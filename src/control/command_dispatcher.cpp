#include "hftp/control/command_dispatcher.h"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>

namespace hftp::control {
namespace {

bool authenticated(const session::Session& session) {
    const std::scoped_lock lock(session.mutex);
    return session.auth == session::AuthState::authenticated;
}

protocol::ReplyCode transfer_error_code(common::Error error) {
    if (error == common::Error::not_found || error == common::Error::permission_denied) {
        return protocol::ReplyCode::file_unavailable;
    }
    if (error == common::Error::socket_error) {
        return protocol::ReplyCode::cannot_open_data;
    }
    return protocol::ReplyCode::transfer_aborted;
}

} // namespace

DispatchAction CommandDispatcher::dispatch(const protocol::Command& command,
                                           session::Session& session,
                                           ReplySink& replies) {
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
        replies.send(protocol::ReplyCode::goodbye, "Goodbye");
        return DispatchAction::close_session;
    }
    if (command.verb == "NOOP") {
        replies.send(protocol::ReplyCode::ok, "Command okay");
        return DispatchAction::continue_session;
    }
    if (command.verb == "HELP") {
        replies.send(protocol::ReplyCode::help,
                     "Basic commands: USER PASS QUIT NOOP HELP TYPE MODE PORT PASV STOR RETR ABOR");
        return DispatchAction::continue_session;
    }

    if (!authenticated(session)) {
        replies.send(protocol::ReplyCode::not_logged_in, "Not logged in");
        return DispatchAction::continue_session;
    }

    if (command.verb == "TYPE") {
        session::TransferType selected_type;
        if (command.argument == "A") {
            selected_type = session::TransferType::ascii;
        } else if (command.argument == "I") {
            selected_type = session::TransferType::binary;
        } else {
            replies.send(protocol::ReplyCode::parameter_error, "TYPE requires A or I");
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

    if (command.verb == "PORT") {
        const auto status = data_.set_active(session, command.argument);
        replies.send(status ? protocol::ReplyCode::ok : protocol::ReplyCode::parameter_error,
                     status ? "Active data endpoint accepted" : status.message);
        return DispatchAction::continue_session;
    }

    if (command.verb == "PASV") {
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
        const auto p1 = static_cast<unsigned int>(port / 256U);
        const auto p2 = static_cast<unsigned int>(port % 256U);
        replies.send(protocol::ReplyCode::passive,
                     "Entering Passive Mode (" + address + ',' + std::to_string(p1) + ',' +
                         std::to_string(p2) + ')');
        return DispatchAction::continue_session;
    }

    if (command.verb == "ABOR") {
        std::uint64_t transfer_id = 0;
        {
            const std::scoped_lock lock(session.mutex);
            transfer_id = session.transfer_id;
        }
        if (!session.request_cancellation()) {
            replies.send(protocol::ReplyCode::bad_sequence, "No active transfer");
            return DispatchAction::continue_session;
        }
        transfers_.request_cancel(transfer_id);
        replies.send(protocol::ReplyCode::ok, "Abort requested");
        return DispatchAction::continue_session;
    }

    if (command.verb == "STOR" || command.verb == "RETR") {
        if (command.argument.empty()) {
            replies.send(protocol::ReplyCode::parameter_error, "File path is required");
            return DispatchAction::continue_session;
        }

        transfer::TransferContext context;
        std::filesystem::path current_directory;
        bool data_mode_missing = false;
        {
            const std::scoped_lock lock(session.mutex);
            if (session.data_mode == session::DataMode::none) {
                data_mode_missing = true;
            } else {
                current_directory = session.current_directory;
                context.type = session.type;
                context.data_mode = session.data_mode;
                context.endpoint = session.active_endpoint;
                if (session.passive_port.has_value()) {
                    context.endpoint = session::UdpEndpoint{data_.passive_address(),
                                                            *session.passive_port};
                }
            }
        }
        if (data_mode_missing) {
            replies.send(protocol::ReplyCode::cannot_open_data,
                         "PORT or PASV is required before transfer");
            return DispatchAction::continue_session;
        }

        const auto file_status = files_.resolve_safe(
            current_directory, std::filesystem::path(command.argument), context.path);
        if (!file_status) {
            replies.send(protocol::ReplyCode::file_unavailable, file_status.message);
            return DispatchAction::continue_session;
        }

        context.transfer_id = next_transfer_id_.fetch_add(1);
        context.direction = command.verb == "STOR" ? transfer::Direction::upload
                                                     : transfer::Direction::download;
        context.cancellation = &session.cancel_requested;
        if (context.data_mode == session::DataMode::passive) {
            context.passive_socket = data_.passive_socket(session.id);
            if (context.passive_socket == network::invalid_socket) {
                replies.send(protocol::ReplyCode::cannot_open_data,
                             "Passive data socket is unavailable");
                return DispatchAction::continue_session;
            }
        }

        if (!session.prepare_transfer(context.transfer_id)) {
            replies.send(protocol::ReplyCode::bad_sequence, "Another transfer is active");
            return DispatchAction::continue_session;
        }

        replies.send(protocol::ReplyCode::opening_data, "Opening data connection");
        if (!session.mark_transfer_running()) {
            session.finish_transfer();
            data_.reset(session);
            replies.send(protocol::ReplyCode::transfer_aborted,
                         "Transfer state transition failed");
            return DispatchAction::continue_session;
        }

        const auto transfer_status = transfers_.start(context);
        session.finish_transfer();
        data_.reset(session);
        replies.send(transfer_status ? protocol::ReplyCode::transfer_complete
                                     : transfer_error_code(transfer_status.error),
                     transfer_status ? "Transfer complete" : transfer_status.message);
        return DispatchAction::continue_session;
    }

    replies.send(protocol::ReplyCode::not_implemented, "Command not implemented");
    return DispatchAction::continue_session;
}

} // namespace hftp::control
