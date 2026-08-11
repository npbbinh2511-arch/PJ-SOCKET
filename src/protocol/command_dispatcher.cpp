#include "hftp/protocol/command_dispatcher.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace hftp::protocol {

// Helper: Format file modification time sang chuỗi FTP "YYYYMMDDHHMMSS"
static std::string format_ftp_time(std::filesystem::file_time_type ftime) {
    using namespace std::chrono;
    // Chuyển file_time_type sang system_clock
    auto s_time = time_point_cast<system_clock::duration>(
        ftime - std::filesystem::file_time_type::clock::now() + system_clock::now()
    );
    std::time_t tt = system_clock::to_time_t(s_time);
    std::tm gm_tm{};
#if defined(_WIN32)
    gmtime_s(&gm_tm, &tt);
#else
    gmtime_r(&tt, &gm_tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&gm_tm, "%Y%m%d%H%M%S");
    return ss.str();
}

CommandDispatcher::CommandDispatcher(const session::SessionService& session_service)
    : m_session_service(session_service) {}

std::string CommandDispatcher::dispatch(session::Session& session, const Command& cmd) {
    
    // 1. Lệnh PWD
    if (cmd.verb == "PWD") {
        std::string current_path = m_session_service.get_pwd(session).string();
        return m_formatter.format(ReplyCode::path_created, "\"" + current_path + "\" is current directory.");
    }

    // 2. Lệnh CWD
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

    // 3. Lệnh CDUP
    if (cmd.verb == "CDUP") {
        auto status = m_session_service.change_to_parent_directory(session);
        if (status.error == common::Error::none) {
            return m_formatter.format(ReplyCode::ok, "Directory successfully changed to parent.");
        }
        return m_formatter.format(ReplyCode::file_unavailable, "Failed to change directory.");
    }

    // 4. Lệnh SIZE <filepath>
    if (cmd.verb == "SIZE") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing file path.");
        }
        
        std::filesystem::path physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "File not found or access denied.");
        }

        std::error_code ec;
        if (!std::filesystem::is_regular_file(physical_path, ec)) {
            return m_formatter.format(ReplyCode::file_unavailable, "Not a regular file.");
        }

        auto file_size = std::filesystem::file_size(physical_path, ec);
        if (ec) {
            return m_formatter.format(ReplyCode::file_unavailable, "Could not retrieve file size.");
        }

        return m_formatter.format(ReplyCode::file_status, std::to_string(file_size));
    }

    // 5. Lệnh MDTM <filepath>
    if (cmd.verb == "MDTM") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing file path.");
        }

        std::filesystem::path physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "File not found or access denied.");
        }

        std::error_code ec;
        if (!std::filesystem::exists(physical_path, ec)) {
            return m_formatter.format(ReplyCode::file_unavailable, "File does not exist.");
        }

        auto last_write = std::filesystem::last_write_time(physical_path, ec);
        if (ec) {
            return m_formatter.format(ReplyCode::file_unavailable, "Could not retrieve modification time.");
        }

        std::string time_str = format_ftp_time(last_write);
        return m_formatter.format(ReplyCode::file_status, time_str);
    }
}

} // namespace hftp::protocol