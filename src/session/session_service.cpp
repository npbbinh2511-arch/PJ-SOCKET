#include "hftp/session/session_service.h"

#include <mutex>
#include <system_error>

namespace hftp::session {

SessionService::SessionService(const filesystem::FileRepository& repository)
    : m_repo(repository) {}

std::filesystem::path SessionService::get_pwd(const Session& session) const {
    const std::scoped_lock lock(session.mutex);
    return session.current_directory;
}

common::Status SessionService::change_directory(
    Session& session, const std::filesystem::path& requested_path) const {
    std::filesystem::path current_directory;
    {
        const std::scoped_lock lock(session.mutex);
        current_directory = session.current_directory;
    }

    std::filesystem::path physical_path;
    const auto resolve_status =
        m_repo.resolve_safe(current_directory, requested_path, physical_path);
    if (!resolve_status) {
        return resolve_status;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(physical_path, error) || error) {
        return {common::Error::not_found, "Directory not found or inaccessible"};
    }

    std::filesystem::path virtual_path;
    const auto normalize_status = filesystem::normalize_virtual_path(
        current_directory, requested_path, virtual_path);
    if (!normalize_status) {
        return normalize_status;
    }
    {
        const std::scoped_lock lock(session.mutex);
        session.current_directory = std::move(virtual_path);
    }
    return {};
}

common::Status SessionService::change_to_parent_directory(Session& session) const {
    return change_directory(session, "..");
}

common::Status SessionService::resolve_path(
    const Session& session, const std::filesystem::path& requested_path,
    std::filesystem::path& out_physical_path) const {
    std::filesystem::path current_directory;
    {
        const std::scoped_lock lock(session.mutex);
        current_directory = session.current_directory;
    }
    return m_repo.resolve_safe(current_directory, requested_path, out_physical_path);
}

} // namespace hftp::session
