#include "hftp/session/session_service.h"
#include <system_error>

namespace hftp::session {

SessionService::SessionService(const filesystem::FileRepository& repo)
    : m_repo(repo) {}

std::filesystem::path SessionService::get_pwd(const Session& session) const {
    std::lock_guard<std::mutex> lock(session.mutex);
    return session.current_directory;
}

common::Status SessionService::change_directory(Session& session, const std::filesystem::path& requested_path) const {
    std::filesystem::path resolved_physical;
    
    std::filesystem::path current_cwd;
    {
        std::lock_guard<std::mutex> lock(session.mutex);
        current_cwd = session.current_directory;
    }

    // 1. Kiểm tra An toàn Sandbox thông qua FileRepository
    auto status = m_repo.resolve_safe(current_cwd, requested_path, resolved_physical);
    if (status.error != common::Error::none) {
        return status;
    }

    // 2. Kiểm tra đường dẫn giải mã có thực sự là thư mục
    // Sử dụng overload với std::error_code để không quăng exception khi dính lỗi IO/permission
    std::error_code ec;
    bool is_dir = std::filesystem::is_directory(resolved_physical, ec);

    if (ec || !is_dir) {
        return common::Status{common::Error::not_found, "Not a directory or access denied"};
    }

    // 3. Cập nhật Virtual CWD mới cho Session
    {
        std::lock_guard<std::mutex> lock(session.mutex);
        session.current_directory = (current_cwd / requested_path).lexically_normal();
    }

    return common::Status{common::Error::none, ""};
}

common::Status SessionService::change_to_parent_directory(Session& session) const {
    return change_directory(session, "..");
}

} // namespace hftp::session