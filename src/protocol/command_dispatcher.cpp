#include "hftp/protocol/command_dispatcher.h"

namespace hftp::protocol {

CommandDispatcher::CommandDispatcher(const session::SessionService& session_service)
    : m_session_service(session_service) {}

std::string CommandDispatcher::dispatch(session::Session& session, const Command& cmd) {
    
    // 1. Lệnh PWD (Print Working Directory)
    if (cmd.verb == "PWD") {
        std::string current_path = m_session_service.get_pwd(session).string();
        return m_formatter.format(ReplyCode::path_created, "\"" + current_path + "\" is current directory.");
    }

    // 2. Lệnh CWD (Change Working Directory)
    if (cmd.verb == "CWD") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error in parameters or arguments.");
        }
        
        auto status = m_session_service.change_directory(session, cmd.argument);
        if (status.error == common::Error::none) {
            return m_formatter.format(ReplyCode::file_action_ok, "Directory successfully changed.");
        }
        return m_formatter.format(ReplyCode::file_unavailable, "Failed to change directory: " + status.message);
    }

    // 3. Lệnh CDUP (Change to Parent Directory)
    if (cmd.verb == "CDUP") {
        auto status = m_session_service.change_to_parent_directory(session);
        if (status.error == common::Error::none) {
            return m_formatter.format(ReplyCode::ok, "Directory successfully changed to parent.");
        }
        return m_formatter.format(ReplyCode::file_unavailable, "Failed to change directory.");
    }

    return m_formatter.format(ReplyCode::not_implemented, "Command not implemented.");
}

} // namespace hftp::protocol