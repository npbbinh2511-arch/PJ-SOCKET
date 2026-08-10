#ifndef HFTP_SESSION_SESSION_SERVICE_H
#define HFTP_SESSION_SESSION_SERVICE_H

#include "hftp/session/session.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/common/result.h"

namespace hftp::session {

class SessionService {
private:
    const filesystem::FileRepository& m_repo;

public:
    explicit SessionService(const filesystem::FileRepository& repo) : m_repo(repo) {}

    // Xử lý lệnh PWD: Trả về virtual path hiện tại
    [[nodiscard]] std::filesystem::path get_pwd(const Session& session) const {
        std::lock_guard<std::mutex> lock(session.mutex);
        return session.current_directory;
    }

    // Xử lý lệnh CWD: Kiểm tra sandbox & thư mục tồn tại trước khi đổi
    common::Status change_directory(Session& session, const std::filesystem::path& requested_path) const {
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
        if (!std::filesystem::is_directory(resolved_physical)) {
            return common::Status{common::Error::not_found, "Not a directory"};
        }

        // 3. Cập nhật Virtual CWD mới cho Session
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            session.current_directory = (current_cwd / requested_path).lexically_normal();
        }

        return common::Status{common::Error::none, ""};
    }
};

} // namespace hftp::session

#endif // HFTP_SESSION_SESSION_SERVICE_H