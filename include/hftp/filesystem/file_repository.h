#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>
#include "hftp/common/result.h"

namespace hftp::filesystem {

struct Entry { 
    std::filesystem::path name; 
    bool directory{false}; 
    std::uintmax_t size{0}; 
};

class FileRepository {
public:
    virtual ~FileRepository() = default;

    [[nodiscard]] virtual common::Status resolve_safe(const std::filesystem::path& cw_dir, const std::filesystem::path& requested, std::filesystem::path& resolved) const = 0;
    [[nodiscard]] virtual common::Status list_file(const std::filesystem::path& virtual_path, std::vector<Entry>& entries) const = 0;
    [[nodiscard]] virtual common::Status read_file(const std::filesystem::path& virtual_path, std::vector<std::uint8_t>& content) const = 0;
    [[nodiscard]] virtual common::Status write_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const = 0;
};

class Std_FileRepository : public FileRepository {
private:
    std::filesystem::path m_root_dir;

public:
    explicit Std_FileRepository(std::filesystem::path root_dir);

    common::Status resolve_safe(const std::filesystem::path& cw_dir, const std::filesystem::path& requested, std::filesystem::path& resolved) const override;
    common::Status list_file(const std::filesystem::path& virtual_path, std::vector<Entry>& entries) const override;
    common::Status read_file(const std::filesystem::path& virtual_path, std::vector<std::uint8_t>& content) const override;
    common::Status write_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const override;
};

}