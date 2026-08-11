#ifndef HFTP_SESSION_SESSION_SERVICE_H
#define HFTP_SESSION_SESSION_SERVICE_H

#include <filesystem>
#include "hftp/common/result.h"
#include "hftp/filesystem/file_repository.h"
#include "hftp/session/session.h"

namespace hftp::session {

class SessionService {
private:
    const filesystem::FileRepository& m_repo;

public:
    explicit SessionService(const filesystem::FileRepository& repo);

    // Xử lý lệnh PWD: Trả về virtual path hiện tại
    [[nodiscard]] std::filesystem::path get_pwd(const Session& session) const;

    // Xử lý lệnh CWD: Kiểm tra sandbox & thư mục tồn tại trước khi đổi
    common::Status change_directory(Session& session, const std::filesystem::path& requested_path) const;

    // Xử lý lệnh CDUP: Chuyển về thư mục cha ("..")
    common::Status change_to_parent_directory(Session& session) const;

    common::Status resolve_path(const Session& session, const std::filesystem::path& requested_path, std::filesystem::path& out_physical_path) const;
};

} // namespace hftp::session

#endif // HFTP_SESSION_SESSION_SERVICE_H