#include "hftp/transfer/file_transfer.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string>
#include <system_error>

#include "hftp/filesystem/ascii_transfer.h"
#include "hftp/integrity/sha256.h"

namespace hftp::transfer {
namespace {

std::vector<std::uint8_t> to_octets(std::span<const std::byte> bytes) {
    std::vector<std::uint8_t> result(bytes.size());
    std::transform(bytes.begin(), bytes.end(), result.begin(),
                   [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    return result;
}

std::vector<std::byte> to_bytes(std::span<const std::uint8_t> octets) {
    std::vector<std::byte> result(octets.size());
    std::transform(octets.begin(), octets.end(), result.begin(),
                   [](std::uint8_t value) { return static_cast<std::byte>(value); });
    return result;
}

common::Status read_local_file(const std::filesystem::path& path,
                               std::vector<std::uint8_t>& content) {
    content.clear();
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        return {common::Error::not_found, "File does not exist"};
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > static_cast<std::uintmax_t>(
                           std::numeric_limits<std::size_t>::max())) {
        return {common::Error::integrity_error, "Unable to determine file size"};
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {common::Error::permission_denied, "Unable to open file"};
    }
    content.resize(static_cast<std::size_t>(size));
    if (!content.empty()) {
        input.read(reinterpret_cast<char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));
        if (static_cast<std::size_t>(input.gcount()) != content.size()) {
            content.clear();
            return {common::Error::integrity_error,
                    "File changed or could not be read completely"};
        }
    }
    return {};
}

common::Status write_local_file(const std::filesystem::path& path,
                                std::span<const std::uint8_t> content,
                                bool append) {
    std::error_code error;
    const auto parent = path.parent_path().empty()
        ? std::filesystem::current_path(error)
        : path.parent_path();
    if (error || !std::filesystem::is_directory(parent, error) || error) {
        return {common::Error::not_found, "Parent directory does not exist"};
    }
    if (std::filesystem::is_directory(path, error)) {
        return {common::Error::invalid_argument, "Target is a directory"};
    }

    if (append) {
        std::ofstream output(path, std::ios::binary | std::ios::app);
        if (!output) {
            return {common::Error::permission_denied, "Unable to append file"};
        }
        if (!content.empty()) {
            output.write(reinterpret_cast<const char*>(content.data()),
                         static_cast<std::streamsize>(content.size()));
        }
        return output ? common::Status{}
                      : common::Status{common::Error::integrity_error,
                                       "Unable to append complete file"};
    }

    auto temporary = path;
    temporary += ".hftp_transfer_tmp";
    for (unsigned int suffix = 0; std::filesystem::exists(temporary, error); ++suffix) {
        if (suffix >= 1024) {
            return {common::Error::busy, "Unable to allocate temporary file"};
        }
        temporary = path;
        temporary += ".hftp_transfer_tmp_" + std::to_string(suffix + 1U);
        error.clear();
    }

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        return {common::Error::permission_denied, "Unable to create temporary file"};
    }
    if (!content.empty()) {
        output.write(reinterpret_cast<const char*>(content.data()),
                     static_cast<std::streamsize>(content.size()));
    }
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        return {common::Error::integrity_error, "Unable to write complete file"};
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        error.clear();
        std::filesystem::copy_file(temporary, path,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
    }
    if (error) {
        return {common::Error::integrity_error,
                "Unable to atomically replace destination: " + error.message()};
    }
    return {};
}

std::string completion_message(std::span<const std::uint8_t> local_bytes,
                               std::span<const std::uint8_t> transferred_bytes,
                               const std::filesystem::path& result_name = {}) {
    std::string message = "Transfer complete bytes=" +
        std::to_string(local_bytes.size()) + " sha256=" +
        integrity::sha256_hex(transferred_bytes);
    if (!result_name.empty()) {
        message += " path=" + result_name.generic_string();
    }
    return message;
}

FileTransferResult as_result(const common::Status& status) {
    return {static_cast<bool>(status), status.message};
}

} // namespace

common::Status FileTransferEngine::start(const TransferContext& context) {
    if (context.transfer_id == 0 || context.cancellation == nullptr) {
        return {common::Error::invalid_argument,
                "Transfer requires an ID and cancellation state"};
    }
    {
        const std::scoped_lock lock(active_mutex_);
        if (!active_cancellations_.emplace(
                context.transfer_id, context.cancellation).second) {
            return {common::Error::busy, "Transfer ID is already active"};
        }
    }
    struct Registration {
        FileTransferEngine& engine;
        std::uint64_t id;
        ~Registration() {
            const std::scoped_lock lock(engine.active_mutex_);
            engine.active_cancellations_.erase(id);
        }
    } registration{*this, context.transfer_id};

    const bool sends_data = context.operation == Operation::retrieve ||
                            context.operation == Operation::detailed_list ||
                            context.operation == Operation::name_list;
    if (sends_data) {
        std::vector<std::uint8_t> local_bytes;
        if (context.operation == Operation::retrieve) {
            auto status = read_local_file(context.path, local_bytes);
            if (!status) {
                return status;
            }
        } else {
            local_bytes = to_octets(context.payload);
        }

        const auto network_octets =
            context.operation == Operation::retrieve &&
                    context.type == session::TransferType::ascii
                ? filesystem::encode_network_ascii(local_bytes)
                : local_bytes;
        const auto network_bytes = to_bytes(network_octets);
        auto sender = rdt::create_stop_and_wait_transport(options_);
        auto status = sender->send(context, network_bytes);
        if (!status) {
            return status;
        }
        return {common::Error::none,
                completion_message(
                    local_bytes, network_octets, context.result_name)};
    }

    auto receiver = rdt::create_stop_and_wait_transport(options_);
    std::vector<std::byte> network_bytes;
    auto status = receiver->receive(context, network_bytes);
    if (!status) {
        if (context.remove_target_on_failure) {
            std::error_code ignored;
            std::filesystem::remove(context.path, ignored);
        }
        return status;
    }

    const auto received_octets = to_octets(network_bytes);
    std::vector<std::uint8_t> local_bytes;
    try {
        local_bytes = context.type == session::TransferType::ascii
            ? filesystem::decode_network_ascii(received_octets)
            : received_octets;
    } catch (const std::exception& error) {
        return {common::Error::protocol_error, error.what()};
    }
    status = write_local_file(context.path, local_bytes,
                              context.operation == Operation::append);
    if (!status) {
        if (context.remove_target_on_failure) {
            std::error_code ignored;
            std::filesystem::remove(context.path, ignored);
        }
        return status;
    }
    return {common::Error::none,
            completion_message(
                local_bytes, received_octets, context.result_name)};
}

void FileTransferEngine::request_cancel(std::uint64_t transfer_id) {
    const std::scoped_lock lock(active_mutex_);
    const auto found = active_cancellations_.find(transfer_id);
    if (found != active_cancellations_.end() && found->second != nullptr) {
        found->second->store(true, std::memory_order_release);
    }
}

FileTransferResult FileTransferEngine::send_file_to_client(
    const std::string& file_path, const TransferContext& context,
    const rdt::StopAndWaitOptions& options) {
    std::vector<std::uint8_t> local_bytes;
    auto status = read_local_file(file_path, local_bytes);
    if (!status) {
        return as_result(status);
    }
    const auto network_octets = context.type == session::TransferType::ascii
        ? filesystem::encode_network_ascii(local_bytes)
        : local_bytes;
    const auto network_bytes = to_bytes(network_octets);
    auto sender = rdt::create_stop_and_wait_transport(options);
    status = sender->send(context, network_bytes);
    if (status) {
        status.message = completion_message(local_bytes, network_octets);
    }
    return as_result(status);
}

FileTransferResult FileTransferEngine::receive_file_from_client(
    const std::string& save_path, const TransferContext& context,
    const rdt::StopAndWaitOptions& options) {
    auto receiver = rdt::create_stop_and_wait_transport(options);
    std::vector<std::byte> network_bytes;
    auto status = receiver->receive(context, network_bytes);
    if (!status) {
        return as_result(status);
    }
    const auto received_octets = to_octets(network_bytes);
    const auto local_bytes = context.type == session::TransferType::ascii
        ? filesystem::decode_network_ascii(received_octets)
        : received_octets;
    status = write_local_file(save_path, local_bytes, false);
    if (status) {
        status.message = completion_message(local_bytes, received_octets);
    }
    return as_result(status);
}

} // namespace hftp::transfer
