#ifndef HFTP_FILESYSTEM_FILE_REPOSITORY_H
#define HFTP_FILESYSTEM_FILE_REPOSITORY_H

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "hftp/common/result.h"

namespace hftp::filesystem {

struct Entry {
    std::filesystem::path name;
    bool directory{false};
    std::uintmax_t size{0};
    std::filesystem::file_time_type modified{};
    std::filesystem::perms permissions{std::filesystem::perms::unknown};
};

struct FileMetadata {
    bool directory{false};
    std::uintmax_t size{0};
    std::filesystem::file_time_type modified{};
    std::filesystem::perms permissions{std::filesystem::perms::unknown};
};

// Chuan hoa duong dan ao FTP. Ket qua luon bat dau tu "/" va khong the
// chua thanh phan ".." thoat ra ngoai virtual root.
[[nodiscard]] common::Status normalize_virtual_path(
    const std::filesystem::path& current_directory,
    const std::filesystem::path& requested,
    std::filesystem::path& normalized);

class FileRepository {
public:
    virtual ~FileRepository() = default;

    [[nodiscard]] virtual common::Status resolve_safe(const std::filesystem::path& cw_dir, const std::filesystem::path& requested, std::filesystem::path& resolved) const = 0;
    [[nodiscard]] virtual common::Status list_file(const std::filesystem::path& virtual_path, std::vector<Entry>& entries) const = 0;
    [[nodiscard]] virtual common::Status read_file(const std::filesystem::path& virtual_path, std::vector<std::uint8_t>& content) const = 0;
    [[nodiscard]] virtual common::Status write_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const = 0;
    [[nodiscard]] virtual common::Status append_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const = 0;
    [[nodiscard]] virtual common::Status write_unique(const std::filesystem::path& virtual_directory, const std::vector<std::uint8_t>& content, std::filesystem::path& created_name) const = 0;
    [[nodiscard]] virtual common::Status metadata(const std::filesystem::path& virtual_path, FileMetadata& result) const = 0;
    [[nodiscard]] virtual common::Status make_directory(const std::filesystem::path& virtual_path) const = 0;
    [[nodiscard]] virtual common::Status remove_directory(const std::filesystem::path& virtual_path) const = 0;
    [[nodiscard]] virtual common::Status remove_file(const std::filesystem::path& virtual_path) const = 0;
    [[nodiscard]] virtual common::Status rename_entry(const std::filesystem::path& from, const std::filesystem::path& to) const = 0;
    [[nodiscard]] virtual common::Status sha256_file(const std::filesystem::path& virtual_path, std::string& hexadecimal_digest) const = 0;
};

class Std_FileRepository : public FileRepository {
private:
    std::filesystem::path m_root_dir;
    mutable std::mutex m_mutation_mutex;

public:
    explicit Std_FileRepository(std::filesystem::path root_dir);

    [[nodiscard]] common::Status resolve_safe(const std::filesystem::path& cw_dir, const std::filesystem::path& requested, std::filesystem::path& resolved) const override;
    [[nodiscard]] common::Status list_file(const std::filesystem::path& virtual_path, std::vector<Entry>& entries) const override;
    [[nodiscard]] common::Status read_file(const std::filesystem::path& virtual_path, std::vector<std::uint8_t>& content) const override;
    [[nodiscard]] common::Status write_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const override;
    [[nodiscard]] common::Status append_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const override;
    [[nodiscard]] common::Status write_unique(const std::filesystem::path& virtual_directory, const std::vector<std::uint8_t>& content, std::filesystem::path& created_name) const override;
    [[nodiscard]] common::Status metadata(const std::filesystem::path& virtual_path, FileMetadata& result) const override;
    [[nodiscard]] common::Status make_directory(const std::filesystem::path& virtual_path) const override;
    [[nodiscard]] common::Status remove_directory(const std::filesystem::path& virtual_path) const override;
    [[nodiscard]] common::Status remove_file(const std::filesystem::path& virtual_path) const override;
    [[nodiscard]] common::Status rename_entry(const std::filesystem::path& from, const std::filesystem::path& to) const override;
    [[nodiscard]] common::Status sha256_file(const std::filesystem::path& virtual_path, std::string& hexadecimal_digest) const override;
};

} // namespace hftp::filesystem

#endif // HFTP_FILESYSTEM_FILE_REPOSITORY_H
