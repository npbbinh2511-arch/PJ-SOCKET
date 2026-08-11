#include "hftp/client/data_channel.h"

#include <atomic>
#include <cstddef>
#include <span>
#include <vector>

#include "hftp/transfer/file_transfer.h"

namespace hftp::client {

common::Status DataChannelClient::make_context(
    const DataTransferSpec& spec, transfer::Direction direction,
    transfer::TransferContext& context) const {
    if (spec.transfer_id == 0 || spec.mode == session::DataMode::none) {
        return {common::Error::invalid_argument,
                "Transfer ID and data mode are required"};
    }

    context = {};
    context.transfer_id = spec.transfer_id;
    context.direction = direction;
    context.type = spec.type;
    context.data_mode = spec.mode;
    context.expected_size = spec.expected_size;
    context.progress = spec.progress;

    if (spec.mode == session::DataMode::passive) {
        if (spec.server_endpoint.port == 0) {
            return {common::Error::invalid_argument,
                    "Passive server endpoint is missing"};
        }
        context.endpoint = spec.server_endpoint;
        if (direction == transfer::Direction::download) {
            context.local_endpoint = session::UdpEndpoint{"0.0.0.0", 0};
            context.peer_discovery = transfer::PeerDiscovery::send_probe;
        }
    } else {
        if (spec.active_local_endpoint.port == 0) {
            return {common::Error::invalid_argument,
                    "Active local endpoint is missing"};
        }
        context.endpoint = spec.active_local_endpoint;
        context.local_endpoint = spec.active_local_endpoint;
        context.bound_socket = spec.bound_socket;
        if (direction == transfer::Direction::upload) {
            context.peer_discovery = transfer::PeerDiscovery::await_probe;
        }
    }
    return {};
}

common::Status DataChannelClient::upload(
    const std::filesystem::path& local_path,
    const DataTransferSpec& spec) const {
    transfer::TransferContext context;
    auto status = make_context(spec, transfer::Direction::upload, context);
    if (!status) {
        return status;
    }
    std::atomic_bool cancelled{false};
    context.cancellation = &cancelled;
    const auto result = transfer::FileTransferEngine::send_file_to_client(
        local_path.string(), context, options_);
    return result.success
        ? common::Status{common::Error::none, result.message}
        : common::Status{common::Error::socket_error, result.message};
}

common::Status DataChannelClient::download(
    const std::filesystem::path& local_path,
    const DataTransferSpec& spec) const {
    transfer::TransferContext context;
    auto status = make_context(spec, transfer::Direction::download, context);
    if (!status) {
        return status;
    }
    std::atomic_bool cancelled{false};
    context.cancellation = &cancelled;
    const auto result = transfer::FileTransferEngine::receive_file_from_client(
        local_path.string(), context, options_);
    return result.success
        ? common::Status{common::Error::none, result.message}
        : common::Status{common::Error::socket_error, result.message};
}

common::Status DataChannelClient::receive_listing(
    std::string& listing, const DataTransferSpec& spec) const {
    listing.clear();
    transfer::TransferContext context;
    auto status = make_context(spec, transfer::Direction::download, context);
    if (!status) {
        return status;
    }
    context.type = session::TransferType::binary;
    std::atomic_bool cancelled{false};
    context.cancellation = &cancelled;
    auto receiver = rdt::create_stop_and_wait_transport(options_);
    std::vector<std::byte> bytes;
    status = receiver->receive(context, bytes);
    if (!status) {
        return status;
    }
    listing.reserve(bytes.size());
    for (const auto value : bytes) {
        listing.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
    }
    return {common::Error::none,
            "Transfer complete bytes=" + std::to_string(bytes.size())};
}

} // namespace hftp::client
