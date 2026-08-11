#include "hftp/control/command_dispatcher.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "hftp/filesystem/listing_formatter.h"

namespace hftp::control {
namespace {

bool authenticated(const session::Session& session) {
    const std::scoped_lock lock(session.mutex);
    return session.auth == session::AuthState::authenticated;
}

std::filesystem::path current_directory(const session::Session& session) {
    const std::scoped_lock lock(session.mutex);
    return session.current_directory;
}

protocol::ReplyCode transfer_error_code(common::Error error) {
    if (error == common::Error::not_found ||
        error == common::Error::permission_denied) {
        return protocol::ReplyCode::file_unavailable;
    }
    if (error == common::Error::socket_error) {
        return protocol::ReplyCode::cannot_open_data;
    }
    return protocol::ReplyCode::transfer_aborted;
}

protocol::ReplyCode file_error_code(common::Error error) {
    if (error == common::Error::busy) {
        return protocol::ReplyCode::action_unavailable;
    }
    return protocol::ReplyCode::file_unavailable;
}

common::Status virtual_path_for(const session::Session& session,
                                const std::filesystem::path& requested,
                                std::filesystem::path& virtual_path) {
    return filesystem::normalize_virtual_path(
        current_directory(session), requested, virtual_path);
}

std::string help_text(std::string_view verb) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 25> help{{
        {"USER", "USER <username>"}, {"PASS", "PASS <password>"},
        {"QUIT", "QUIT"}, {"NOOP", "NOOP"}, {"PWD", "PWD"},
        {"CWD", "CWD <path>"}, {"CDUP", "CDUP"}, {"MKD", "MKD <dirname>"},
        {"RMD", "RMD <dirname>"}, {"LIST", "LIST [path]"},
        {"NLST", "NLST [path]"}, {"STAT", "STAT [path]"},
        {"SIZE", "SIZE <filename>"}, {"MDTM", "MDTM <filename>"},
        {"TYPE", "TYPE {A | I}"}, {"MODE", "MODE S"},
        {"PORT", "PORT <h1,h2,h3,h4,p1,p2>"}, {"PASV", "PASV"},
        {"RETR", "RETR <filename>"}, {"STOR", "STOR <filename>"},
        {"STOU", "STOU"}, {"APPE", "APPE <filename>"},
        {"DELE", "DELE <filename>"}, {"HASH", "HASH <filename>"},
        {"ABOR", "ABOR"},
    }};
    if (!verb.empty()) {
        const auto found = std::find_if(help.begin(), help.end(),
            [verb](const auto& item) { return item.first == verb; });
        return found == help.end() ? std::string{} : std::string(found->second);
    }
    std::string result;
    for (const auto& [name, syntax] : help) {
        static_cast<void>(syntax);
        if (!result.empty()) {
            result += ' ';
        }
        result += name;
    }
    result += " RNFR RNTO HELP";
    return result;
}

bool is_data_transfer_command(std::string_view verb) {
    return verb == "STOR" || verb == "RETR" || verb == "STOU" ||
           verb == "APPE" || verb == "LIST" || verb == "NLST";
}

} // namespace

CommandDispatcher::~CommandDispatcher() {
    std::vector<std::shared_ptr<TransferJob>> jobs;
    {
        const std::scoped_lock lock(jobs_mutex_);
        for (auto& [session_id, job] : jobs_) {
            static_cast<void>(session_id);
            jobs.push_back(std::move(job));
        }
        jobs_.clear();
    }
}

void CommandDispatcher::remove_finished_job(std::uint64_t session_id) {
    std::shared_ptr<TransferJob> finished;
    {
        const std::scoped_lock lock(jobs_mutex_);
        const auto found = jobs_.find(session_id);
        if (found != jobs_.end() && found->second->done.load()) {
            finished = std::move(found->second);
            jobs_.erase(found);
        }
    }
    if (finished && finished->worker.joinable()) {
        finished->worker.join();
    }
}

void CommandDispatcher::wait_for_job(std::uint64_t session_id) noexcept {
    std::shared_ptr<TransferJob> job;
    {
        const std::scoped_lock lock(jobs_mutex_);
        const auto found = jobs_.find(session_id);
        if (found != jobs_.end()) {
            job = std::move(found->second);
            jobs_.erase(found);
        }
    }
    if (job && job->worker.joinable()) {
        job->worker.join();
    }
}

std::shared_ptr<CommandDispatcher::TransferJob> CommandDispatcher::find_job(
    std::uint64_t session_id) noexcept {
    const std::scoped_lock lock(jobs_mutex_);
    const auto found = jobs_.find(session_id);
    return found == jobs_.end() ? nullptr : found->second;
}

void CommandDispatcher::end_session(session::Session& session) noexcept {
    std::uint64_t transfer_id = 0;
    bool active = false;
    {
        const std::scoped_lock lock(session.mutex);
        transfer_id = session.transfer_id;
        active = session.transfer != session::TransferState::idle;
    }
    if (active) {
        static_cast<void>(session.request_cancellation());
        transfers_.request_cancel(transfer_id);
    }
    wait_for_job(session.id);
    data_.reset(session);
}

DispatchAction CommandDispatcher::dispatch(const protocol::Command& command,
                                           session::Session& session,
                                           ReplySink& replies) {
    remove_finished_job(session.id);

    if (command.verb == "USER") {
        const auto status = auth_.user(session, command.argument);
        replies.send(status ? protocol::ReplyCode::need_password
                            : protocol::ReplyCode::parameter_error,
                     status ? "Username okay, need password" : status.message);
        return DispatchAction::continue_session;
    }

    if (command.verb == "PASS") {
        const auto status = auth_.pass(session, command.argument);
        if (status) {
            replies.send(protocol::ReplyCode::logged_in, "Login successful");
        } else if (status.error == common::Error::protocol_error) {
            replies.send(protocol::ReplyCode::bad_sequence, status.message);
        } else {
            replies.send(protocol::ReplyCode::not_logged_in, status.message);
        }
        return DispatchAction::continue_session;
    }

    if (command.verb == "QUIT") {
        end_session(session);
        replies.send(protocol::ReplyCode::goodbye, "Goodbye");
        return DispatchAction::close_session;
    }
    if (command.verb == "NOOP") {
        replies.send(protocol::ReplyCode::ok, "Command okay");
        return DispatchAction::continue_session;
    }
    if (command.verb == "HELP") {
        const auto text = help_text(command.argument);
        replies.send(text.empty() ? protocol::ReplyCode::parameter_error
                                  : protocol::ReplyCode::help,
                     text.empty() ? "Unknown command" : text);
        return DispatchAction::continue_session;
    }

    if (!authenticated(session)) {
        replies.send(protocol::ReplyCode::not_logged_in, "Not logged in");
        return DispatchAction::continue_session;
    }

    if (command.verb == "SYST") {
        replies.send(protocol::ReplyCode::system_type, "UNIX Type: L8");
        return DispatchAction::continue_session;
    }
    if (command.verb == "PWD") {
        replies.send(protocol::ReplyCode::path_created,
                     "\"" + current_directory(session).generic_string() +
                         "\" is current directory");
        return DispatchAction::continue_session;
    }
    if (command.verb == "CWD" || command.verb == "CDUP") {
        const auto requested = command.verb == "CDUP"
            ? std::filesystem::path("..")
            : std::filesystem::path(command.argument);
        if (requested.empty()) {
            replies.send(protocol::ReplyCode::parameter_error,
                         "Directory path is required");
            return DispatchAction::continue_session;
        }

        const auto previous = current_directory(session);
        std::filesystem::path physical_path;
        auto status = files_.resolve_safe(previous, requested, physical_path);
        std::error_code error;
        if (status &&
            (!std::filesystem::is_directory(physical_path, error) || error)) {
            status = {common::Error::not_found, "Directory not found"};
        }
        std::filesystem::path virtual_path;
        if (status) {
            status = filesystem::normalize_virtual_path(
                previous, requested, virtual_path);
        }
        if (!status) {
            replies.send(protocol::ReplyCode::file_unavailable, status.message);
            return DispatchAction::continue_session;
        }
        {
            const std::scoped_lock lock(session.mutex);
            session.current_directory = std::move(virtual_path);
        }
        replies.send(protocol::ReplyCode::file_action_ok,
                     "Directory successfully changed");
        return DispatchAction::continue_session;
    }

    if (command.verb == "TYPE") {
        session::TransferType selected_type;
        if (command.argument == "A") {
            selected_type = session::TransferType::ascii;
        } else if (command.argument == "I") {
            selected_type = session::TransferType::binary;
        } else {
            replies.send(protocol::ReplyCode::parameter_error,
                         "TYPE requires A or I");
            return DispatchAction::continue_session;
        }
        {
            const std::scoped_lock lock(session.mutex);
            session.type = selected_type;
        }
        replies.send(protocol::ReplyCode::ok, "Transfer type updated");
        return DispatchAction::continue_session;
    }

    if (command.verb == "MODE") {
        if (command.argument != "S") {
            replies.send(protocol::ReplyCode::not_implemented,
                         "Only stream mode is implemented");
            return DispatchAction::continue_session;
        }
        {
            const std::scoped_lock lock(session.mutex);
            session.mode = session::TransferMode::stream;
        }
        replies.send(protocol::ReplyCode::ok, "Stream mode selected");
        return DispatchAction::continue_session;
    }
    if (command.verb == "STRU") {
        replies.send(command.argument == "F" ? protocol::ReplyCode::ok
                                               : protocol::ReplyCode::parameter_error,
                     command.argument == "F" ? "File structure selected"
                                               : "STRU requires F");
        return DispatchAction::continue_session;
    }

    if (command.verb == "STAT") {
        if (command.argument.empty()) {
            std::string state;
            {
                const std::scoped_lock lock(session.mutex);
                state = session.transfer == session::TransferState::idle
                    ? "idle" : "transferring";
            }
            replies.send(protocol::ReplyCode::status,
                         "Hybrid FTP session=" + std::to_string(session.id) +
                             " cwd=" + current_directory(session).generic_string() +
                             " state=" + state);
            return DispatchAction::continue_session;
        }

        std::filesystem::path virtual_path;
        auto status = virtual_path_for(session, command.argument, virtual_path);
        filesystem::FileMetadata metadata;
        if (status) {
            status = files_.metadata(virtual_path, metadata);
        }
        if (!status) {
            replies.send(file_error_code(status.error), status.message);
            return DispatchAction::continue_session;
        }
        replies.send(protocol::ReplyCode::file_status,
                     "path=" + virtual_path.generic_string() +
                         " type=" + (metadata.directory ? "directory" : "file") +
                         " size=" + std::to_string(metadata.size) +
                         " modified=" +
                         filesystem::format_ftp_timestamp(metadata.modified));
        return DispatchAction::continue_session;
    }

    if (command.verb == "SIZE" || command.verb == "MDTM" ||
        command.verb == "HASH") {
        if (command.argument.empty()) {
            replies.send(protocol::ReplyCode::parameter_error,
                         "File path is required");
            return DispatchAction::continue_session;
        }
        std::filesystem::path virtual_path;
        auto status = virtual_path_for(session, command.argument, virtual_path);
        if (!status) {
            replies.send(protocol::ReplyCode::file_unavailable, status.message);
            return DispatchAction::continue_session;
        }
        if (command.verb == "HASH") {
            std::string digest;
            status = files_.sha256_file(virtual_path, digest);
            replies.send(status ? protocol::ReplyCode::file_status
                                : file_error_code(status.error),
                         status ? digest : status.message);
            return DispatchAction::continue_session;
        }
        filesystem::FileMetadata metadata;
        status = files_.metadata(virtual_path, metadata);
        if (!status || metadata.directory) {
            replies.send(protocol::ReplyCode::file_unavailable,
                         status ? "Path is not a regular file" : status.message);
            return DispatchAction::continue_session;
        }
        replies.send(protocol::ReplyCode::file_status,
                     command.verb == "SIZE"
                         ? std::to_string(metadata.size)
                         : filesystem::format_ftp_timestamp(metadata.modified));
        return DispatchAction::continue_session;
    }

    if (command.verb == "MKD" || command.verb == "RMD" ||
        command.verb == "DELE") {
        if (command.argument.empty()) {
            replies.send(protocol::ReplyCode::parameter_error,
                         "Path is required");
            return DispatchAction::continue_session;
        }
        std::filesystem::path virtual_path;
        auto status = virtual_path_for(session, command.argument, virtual_path);
        if (status) {
            if (command.verb == "MKD") {
                status = files_.make_directory(virtual_path);
            } else if (command.verb == "RMD") {
                status = files_.remove_directory(virtual_path);
            } else {
                status = files_.remove_file(virtual_path);
            }
        }
        const auto success_code = command.verb == "MKD"
            ? protocol::ReplyCode::path_created
            : protocol::ReplyCode::file_action_ok;
        replies.send(status ? success_code : file_error_code(status.error),
                     status ? "File action successful" : status.message);
        return DispatchAction::continue_session;
    }

    if (command.verb == "RNFR") {
        if (command.argument.empty()) {
            replies.send(protocol::ReplyCode::parameter_error,
                         "Source path is required");
            return DispatchAction::continue_session;
        }
        std::filesystem::path virtual_path;
        auto status = virtual_path_for(session, command.argument, virtual_path);
        filesystem::FileMetadata metadata;
        if (status) {
            status = files_.metadata(virtual_path, metadata);
        }
        if (!status) {
            replies.send(protocol::ReplyCode::file_unavailable, status.message);
            return DispatchAction::continue_session;
        }
        {
            const std::scoped_lock lock(session.mutex);
            session.rename_from = std::move(virtual_path);
        }
        replies.send(protocol::ReplyCode::rename_pending,
                     "Requested file action pending RNTO");
        return DispatchAction::continue_session;
    }

    if (command.verb == "RNTO") {
        std::optional<std::filesystem::path> from;
        {
            const std::scoped_lock lock(session.mutex);
            from = std::move(session.rename_from);
            session.rename_from.reset();
        }
        if (!from) {
            replies.send(protocol::ReplyCode::bad_sequence,
                         "RNFR is required before RNTO");
            return DispatchAction::continue_session;
        }
        std::filesystem::path to;
        auto status = virtual_path_for(session, command.argument, to);
        if (status) {
            status = files_.rename_entry(*from, to);
        }
        replies.send(status ? protocol::ReplyCode::file_action_ok
                            : file_error_code(status.error),
                     status ? "File action successful" : status.message);
        return DispatchAction::continue_session;
    }

    if (command.verb == "PORT") {
        {
            const std::scoped_lock lock(session.mutex);
            if (session.transfer != session::TransferState::idle) {
                replies.send(protocol::ReplyCode::bad_sequence,
                             "Cannot change data mode during a transfer");
                return DispatchAction::continue_session;
            }
        }
        const auto status = data_.set_active(session, command.argument);
        replies.send(status ? protocol::ReplyCode::ok
                            : protocol::ReplyCode::parameter_error,
                     status ? "Active data endpoint accepted" : status.message);
        return DispatchAction::continue_session;
    }

    if (command.verb == "PASV") {
        {
            const std::scoped_lock lock(session.mutex);
            if (session.transfer != session::TransferState::idle) {
                replies.send(protocol::ReplyCode::bad_sequence,
                             "Cannot change data mode during a transfer");
                return DispatchAction::continue_session;
            }
        }
        const auto status = data_.open_passive(session);
        if (!status) {
            replies.send(protocol::ReplyCode::cannot_open_data, status.message);
            return DispatchAction::continue_session;
        }

        std::uint16_t port = 0;
        {
            const std::scoped_lock lock(session.mutex);
            port = *session.passive_port;
        }
        std::string address = data_.passive_address();
        std::replace(address.begin(), address.end(), '.', ',');
        replies.send(protocol::ReplyCode::passive,
                     "Entering Passive Mode (" + address + ',' +
                         std::to_string(port / 256U) + ',' +
                         std::to_string(port % 256U) + ')');
        return DispatchAction::continue_session;
    }

    if (command.verb == "ABOR") {
        const auto job = find_job(session.id);
        if (!job) {
            replies.send(protocol::ReplyCode::bad_sequence, "No active transfer");
            return DispatchAction::continue_session;
        }

        // If completion won the race, its terminal reply is sent first and the
        // state check below correctly rejects this stale ABOR. Otherwise this
        // lock keeps the worker's terminal reply behind the ABOR acknowledgement.
        const std::scoped_lock completion_lock(job->completion_mutex);
        std::uint64_t transfer_id = 0;
        {
            const std::scoped_lock lock(session.mutex);
            transfer_id = session.transfer_id;
        }
        if (!session.request_cancellation()) {
            replies.send(protocol::ReplyCode::bad_sequence, "No active transfer");
            return DispatchAction::continue_session;
        }
        replies.send(protocol::ReplyCode::ok, "Abort requested");
        transfers_.request_cancel(transfer_id);
        return DispatchAction::continue_session;
    }

    if (is_data_transfer_command(command.verb)) {
        const bool path_required = command.verb == "STOR" ||
                                   command.verb == "RETR" ||
                                   command.verb == "APPE";
        if (path_required && command.argument.empty()) {
            replies.send(protocol::ReplyCode::parameter_error,
                         "File path is required");
            return DispatchAction::continue_session;
        }
        if (command.verb == "STOU" && !command.argument.empty()) {
            replies.send(protocol::ReplyCode::parameter_error,
                         "STOU does not accept a filename");
            return DispatchAction::continue_session;
        }

        transfer::TransferContext context;
        std::filesystem::path session_directory;
        {
            const std::scoped_lock lock(session.mutex);
            if (session.data_mode == session::DataMode::none) {
                replies.send(protocol::ReplyCode::cannot_open_data,
                             "PORT or PASV is required before transfer");
                return DispatchAction::continue_session;
            }
            session_directory = session.current_directory;
            context.type = session.type;
            context.data_mode = session.data_mode;
            context.endpoint = session.active_endpoint;
            if (session.passive_port) {
                context.endpoint = session::UdpEndpoint{
                    data_.passive_address(), *session.passive_port};
            }
        }

        std::optional<std::filesystem::path> reserved_unique_path;
        if (command.verb == "LIST" || command.verb == "NLST") {
            std::filesystem::path virtual_path;
            auto status = filesystem::normalize_virtual_path(
                session_directory,
                command.argument.empty() ? std::filesystem::path(".")
                                         : std::filesystem::path(command.argument),
                virtual_path);
            std::vector<filesystem::Entry> entries;
            if (status) {
                status = files_.list_file(virtual_path, entries);
            }
            if (!status) {
                filesystem::FileMetadata metadata;
                const auto metadata_status = files_.metadata(virtual_path, metadata);
                if (!metadata_status) {
                    replies.send(file_error_code(status.error), status.message);
                    return DispatchAction::continue_session;
                }
                entries.push_back({virtual_path.filename(), metadata.directory,
                                   metadata.size, metadata.modified,
                                   metadata.permissions});
            }
            const auto listing = command.verb == "LIST"
                ? filesystem::format_detailed_listing(entries)
                : filesystem::format_name_listing(entries);
            context.payload.reserve(listing.size());
            for (const unsigned char value : listing) {
                context.payload.push_back(static_cast<std::byte>(value));
            }
            context.operation = command.verb == "LIST"
                ? transfer::Operation::detailed_list
                : transfer::Operation::name_list;
            context.direction = transfer::Direction::download;
            context.type = session::TransferType::binary;
            context.result_name = virtual_path;
            context.expected_size = static_cast<std::uint64_t>(listing.size());
        } else if (command.verb == "STOU") {
            std::filesystem::path created_name;
            auto status = files_.write_unique(session_directory, {}, created_name);
            if (!status) {
                replies.send(file_error_code(status.error), status.message);
                return DispatchAction::continue_session;
            }
            std::filesystem::path virtual_path;
            status = filesystem::normalize_virtual_path(
                session_directory, created_name, virtual_path);
            if (status) {
                status = files_.resolve_safe(session_directory, created_name,
                                             context.path);
            }
            if (!status) {
                static_cast<void>(files_.remove_file(virtual_path));
                replies.send(file_error_code(status.error), status.message);
                return DispatchAction::continue_session;
            }
            reserved_unique_path = virtual_path;
            context.result_name = virtual_path;
            context.operation = transfer::Operation::store_unique;
            context.direction = transfer::Direction::upload;
            context.remove_target_on_failure = true;
        } else {
            const auto path_status = files_.resolve_safe(
                session_directory, command.argument, context.path);
            if (!path_status) {
                replies.send(protocol::ReplyCode::file_unavailable,
                             path_status.message);
                return DispatchAction::continue_session;
            }
            if (command.verb == "RETR") {
                std::filesystem::path virtual_path;
                auto status = filesystem::normalize_virtual_path(
                    session_directory, command.argument, virtual_path);
                filesystem::FileMetadata metadata;
                if (status) {
                    status = files_.metadata(virtual_path, metadata);
                }
                if (!status || metadata.directory) {
                    replies.send(protocol::ReplyCode::file_unavailable,
                                 status ? "Path is not a regular file"
                                        : status.message);
                    return DispatchAction::continue_session;
                }
                context.operation = transfer::Operation::retrieve;
                context.direction = transfer::Direction::download;
                context.expected_size = static_cast<std::uint64_t>(metadata.size);
            } else {
                context.operation = command.verb == "APPE"
                    ? transfer::Operation::append
                    : transfer::Operation::store;
                context.direction = transfer::Direction::upload;
            }
        }

        context.transfer_id = next_transfer_id_.fetch_add(1);
        context.cancellation = &session.cancel_requested;
        if (context.data_mode == session::DataMode::passive) {
            context.bound_socket = data_.acquire_passive_socket(session.id);
            if (!context.bound_socket || !context.bound_socket->valid()) {
                replies.send(protocol::ReplyCode::cannot_open_data,
                             "Passive data socket is unavailable");
                return DispatchAction::continue_session;
            }
            if (context.direction == transfer::Direction::download) {
                context.peer_discovery = transfer::PeerDiscovery::await_probe;
            }
        } else if (context.direction == transfer::Direction::upload) {
            // In UDP Active upload the server opens an ephemeral receive socket,
            // probes the client's advertised PORT, then accepts data only from
            // the discovered peer.
            context.peer_discovery = transfer::PeerDiscovery::send_probe;
            context.local_endpoint = session::UdpEndpoint{"0.0.0.0", 0};
        }

        if (!session.prepare_transfer(context.transfer_id)) {
            if (reserved_unique_path) {
                static_cast<void>(files_.remove_file(*reserved_unique_path));
            }
            replies.send(protocol::ReplyCode::bad_sequence,
                         "Another transfer is active");
            return DispatchAction::continue_session;
        }
        if (!session.mark_transfer_running()) {
            if (reserved_unique_path) {
                static_cast<void>(files_.remove_file(*reserved_unique_path));
            }
            session.finish_transfer();
            data_.reset(session);
            replies.send(protocol::ReplyCode::transfer_aborted,
                         "Transfer state transition failed");
            return DispatchAction::continue_session;
        }

        std::string opening = "Opening data connection id=" +
                              std::to_string(context.transfer_id);
        if (context.direction == transfer::Direction::download) {
            opening += " bytes=" + std::to_string(context.expected_size);
        }
        if (!context.result_name.empty()) {
            opening += " path=" + context.result_name.generic_string();
        }
        replies.send(protocol::ReplyCode::opening_data, std::move(opening));
        auto job = std::make_shared<TransferJob>();
        {
            const std::scoped_lock lock(jobs_mutex_);
            jobs_[session.id] = job;
        }
        try {
            job->worker = std::jthread(
                [this, &session, &replies, context = std::move(context), job] {
                    common::Status status;
                    try {
                        status = transfers_.start(context);
                    } catch (const std::exception& error) {
                        status = {common::Error::integrity_error, error.what()};
                    } catch (...) {
                        status = {common::Error::integrity_error,
                                  "Transfer worker failed"};
                    }
                    {
                        const std::scoped_lock completion_lock(
                            job->completion_mutex);
                        session.finish_transfer();
                        data_.reset(session);
                        replies.send(status ? protocol::ReplyCode::transfer_complete
                                            : transfer_error_code(status.error),
                                     status
                                         ? (status.message.empty()
                                                ? "Transfer complete"
                                                : status.message)
                                         : status.message);
                        job->done.store(true, std::memory_order_release);
                    }
                });
        } catch (const std::system_error& error) {
            {
                const std::scoped_lock lock(jobs_mutex_);
                jobs_.erase(session.id);
            }
            session.finish_transfer();
            data_.reset(session);
            replies.send(protocol::ReplyCode::transfer_aborted,
                         "Unable to start transfer worker: " +
                             std::string(error.what()));
        }
        return DispatchAction::continue_session;
    }

    replies.send(protocol::ReplyCode::not_implemented, "Command not implemented");
    return DispatchAction::continue_session;
}

} // namespace hftp::control
