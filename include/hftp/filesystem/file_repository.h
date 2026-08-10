#ifndef HFTP_FILESYSTEM_FILE_REPOSITORY_H
#define HFTP_FILESYSTEM_FILE_REPOSITORY_H

#include <cstdint>
#include <filesystem>
#include <vector>
#include "hftp/common/result.h"

namespace hftp::filesystem {

struct Entry { std::filesystem::path name; bool directory{}; std::uintmax_t size{}; };

class FileRepository {
public:
    virtual ~FileRepository() = default;
    [[nodiscard]] virtual common::Status resolve_safe(
        const std::filesystem::path& cwd, const std::filesystem::path& requested,
        std::filesystem::path& resolved) const = 0;
    [[nodiscard]] virtual common::Status list(
        const std::filesystem::path& path, std::vector<Entry>& entries) const = 0;

    // TODO(C):
    // - Canonicalize against the configured server root and reject every escape attempt.
    // - Add narrow ASCII read/write operations first; binary and mutation APIs later.
    // - Keep FTP reply codes and session state outside this interface.
    // - Tests: normal child, '..', absolute path, symlink/junction escape, missing path.
};

} // namespace hftp::filesystem

#endif // HFTP_FILESYSTEM_FILE_REPOSITORY_H
