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

CommandDispatcher::CommandDispatcher(const session::SessionService& session_service) : m_session_service(session_service) {}

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

    // 6. Lệnh MKD <dirname> (Make Directory)
    if (cmd.verb == "MKD") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing directory name.");
        }

        std::filesystem::path physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "Access denied or invalid path.");
        }

        std::error_code ec;
        if (std::filesystem::exists(physical_path, ec)) {
            return m_formatter.format(ReplyCode::file_unavailable, "Directory or file already exists.");
        }

        if (!std::filesystem::create_directory(physical_path, ec) || ec) {
            return m_formatter.format(ReplyCode::file_unavailable, "Could not create directory.");
        }

        return m_formatter.format(ReplyCode::path_created, "\"" + cmd.argument + "\" directory created.");
    }

    // 7. Lệnh RMD <dirname> (Remove Directory)
    if (cmd.verb == "RMD") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing directory name.");
        }

        std::filesystem::path physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "Access denied or invalid path.");
        }

        std::error_code ec;
        if (!std::filesystem::is_directory(physical_path, ec)) {
            return m_formatter.format(ReplyCode::file_unavailable, "Not a directory.");
        }

        if (!std::filesystem::remove(physical_path, ec) || ec) {
            return m_formatter.format(ReplyCode::file_unavailable, "Could not remove directory (must be empty).");
        }

        return m_formatter.format(ReplyCode::file_action_ok, "Directory removed.");
    }

    // 8. Lệnh DELE <filename> (Delete File)
    if (cmd.verb == "DELE") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing filename.");
        }

        std::filesystem::path physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "Access denied or invalid path.");
        }

        std::error_code ec;
        if (std::filesystem::is_directory(physical_path, ec)) {
            return m_formatter.format(ReplyCode::file_unavailable, "Path is a directory, use RMD instead.");
        }

        if (!std::filesystem::remove(physical_path, ec) || ec) {
            return m_formatter.format(ReplyCode::file_unavailable, "Could not delete file.");
        }

        return m_formatter.format(ReplyCode::file_action_ok, "File deleted.");
    }

    return m_formatter.format(ReplyCode::not_implemented, "Command not implemented.");

    // 9. Lệnh RNFR <old_path> (Rename From)
    if (cmd.verb == "RNFR") {
        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing file path.");
        }

        std::filesystem::path physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "File or directory not found.");
        }

        std::error_code ec;
        if (!std::filesystem::exists(physical_path, ec)) {
            return m_formatter.format(ReplyCode::file_unavailable, "File or directory does not exist.");
        }

        // Lưu đường dẫn gốc vào session và chờ RNTO
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            session.rename_from = physical_path;
        }

        return m_formatter.format(ReplyCode::rename_pending, "Requested file action pending further information.");
    }

    // 10. Lệnh RNTO <new_path> (Rename To)
    if (cmd.verb == "RNTO") {
        std::optional<std::filesystem::path> old_physical_path;
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            old_physical_path = session.rename_from;
            session.rename_from.reset(); // Reset trạng thái RNFR sau khi xử lý
        }

        // Bắt buộc phải gọi RNFR thành công trước đó (kiểm tra sequence error)
        if (!old_physical_path.has_value()) {
            return m_formatter.format(ReplyCode::bad_sequence, "Bad sequence of commands. Send RNFR first.");
        }

        if (cmd.argument.empty()) {
            return m_formatter.format(ReplyCode::parameter_error, "Syntax error: missing new file path.");
        }

        std::filesystem::path new_physical_path;
        auto status = m_session_service.resolve_path(session, cmd.argument, new_physical_path);
        if (status.error != common::Error::none) {
            return m_formatter.format(ReplyCode::file_unavailable, "Access denied or invalid target path.");
        }

        std::error_code ec;
        std::filesystem::rename(*old_physical_path, new_physical_path, ec);
        if (ec) {
            return m_formatter.format(ReplyCode::file_unavailable, "Failed to rename file or directory.");
        }

        return m_formatter.format(ReplyCode::file_action_ok, "File action successful.");
    }

    // 11. Lệnh TYPE <A|I>
    if (cmd.verb == "TYPE") {
        if (cmd.argument == "A" || cmd.argument == "a") {
            return m_formatter.format(ReplyCode::ok, "Type set to A.");
        }
        if (cmd.argument == "I" || cmd.argument == "i" || cmd.argument == "L 8") {
            return m_formatter.format(ReplyCode::ok, "Type set to I.");
        }
        return m_formatter.format(ReplyCode::parameter_error, "Type not supported. Use A or I.");
    }

    // 12. Lệnh MODE <S>
    if (cmd.verb == "MODE") {
        if (cmd.argument == "S" || cmd.argument == "s") {
            return m_formatter.format(ReplyCode::ok, "Mode set to S.");
        }
        return m_formatter.format(ReplyCode::parameter_error, "Only Stream mode (S) is supported.");
    }

    // 13. Lệnh STRU <F>
    if (cmd.verb == "STRU") {
        if (cmd.argument == "F" || cmd.argument == "f") {
            return m_formatter.format(ReplyCode::ok, "Structure set to F.");
        }
        return m_formatter.format(ReplyCode::parameter_error, "Only File structure (F) is supported.");
    }

    // 14. Lệnh NOOP
    if (cmd.verb == "NOOP") {
        return m_formatter.format(ReplyCode::ok, "OK.");
    }

    // 15. Lệnh SYST (System)
    if (cmd.verb == "SYST") {
        return m_formatter.format(ReplyCode::system_type, "UNIX Type: L8");
    }

    // 16. Lệnh QUIT (Logout)
    if (cmd.verb == "QUIT") {
        return m_formatter.format(ReplyCode::goodbye, "Goodbye.");
    }
}

} // namespace hftp::protocol