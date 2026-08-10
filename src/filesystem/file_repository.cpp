#include "hftp/filesystem/file_repository.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <random>
#include <string>

namespace hftp::filesystem {

static bool is_subpath(const std::filesystem::path& base, const std::filesystem::path& target) {
    auto b_it = base.begin();
    auto t_it = target.begin();

    for (; b_it != base.end() && t_it != target.end(); ++b_it, ++t_it) {
        std::string b_str = b_it->string();
        std::string t_str = t_it->string();
    
#if defined(_WIN32)
        std::transform(b_str.begin(), b_str.end(), b_str.begin(), ::tolower);
        std::transform(t_str.begin(), t_str.end(), t_str.begin(), ::tolower);

#endif
        if (b_str != t_str) return false;
    }

    return b_it == base.end(); // Base phải là prefix hoàn chỉnh của target
}

Std_FileRepository::Std_FileRepository(std::filesystem::path root_dir) {
    std::error_code ec;
    if (!std::filesystem::exists(root_dir, ec) || !std::filesystem::is_directory(root_dir, ec)) {
        throw std::invalid_argument("Root directory does not exist or is not a directory");
    }

    m_root_dir = std::filesystem::canonical(root_dir, ec);
    if (ec) {
        throw std::invalid_argument("Failed to canonicalize root directory");
    }
}

common::Status Std_FileRepository::resolve_safe(const std::filesystem::path& cw_dir, const std::filesystem::path& requested, std::filesystem::path& resolved) const {
    std::error_code ec;
    
    std::filesystem::path combined = m_root_dir / cw_dir.relative_path() / requested.relative_path();
    std::filesystem::path normalized = std::filesystem::weakly_canonical(combined, ec);

    if (ec) {
        return common::Status{common::Error::invalid_argument, "Invalid path resolution"};
    }

    if (!is_subpath(m_root_dir, normalized)) {
        return common::Status{common::Error::permission_denied, "Access denied: Outside sandbox"};
    }

    resolved = normalized;
    return common::Status{common::Error::none};
}

common::Status Std_FileRepository::list_file(const std::filesystem::path& virtual_path, std::vector<Entry>& entries) const {
    entries.clear();
    std::filesystem::path real_path;
    
    auto status = resolve_safe("/", virtual_path, real_path);
    if (status.error != common::Error::none) return status;

    std::error_code ec;
    if (!std::filesystem::exists(real_path, ec) || !std::filesystem::is_directory(real_path, ec)) {
        return common::Status{common::Error::not_found, "Directory not found"};
    }

    auto iter = std::filesystem::directory_iterator(real_path, ec);
    if (ec) return common::Status{common::Error::permission_denied, "Cannot read directory"};

    for (const auto& entry : iter) {
        Entry item;
        item.name = entry.path().filename();
        item.directory = entry.is_directory(ec);
        if (ec) return common::Status{common::Error::integrity_error, "Failed to read entry type"};

        item.size = item.directory ? 0 : entry.file_size(ec);
        if (ec && !item.directory) return common::Status{common::Error::integrity_error, "Failed to read file size"};

        entries.push_back(item);
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {return a.name < b.name;});

    return common::Status{common::Error::none};
}

common::Status Std_FileRepository::read_file(const std::filesystem::path& virtual_path, std::vector<std::uint8_t>& content) const {
    std::filesystem::path real_path;
    auto status = resolve_safe("/", virtual_path, real_path);
    if (status.error != common::Error::none) return status;
    std::error_code ec;

    if (!std::filesystem::exists(real_path, ec) || !std::filesystem::is_regular_file(real_path, ec)) {
        return common::Status{common::Error::not_found, "File not found"};
    }

    auto size = std::filesystem::file_size(real_path, ec);
    if (ec || size > SIZE_MAX) {
        return common::Status{common::Error::invalid_argument, "File size invalid or too large"};
    }

    std::ifstream file(real_path, std::ios::binary);
    if (!file.is_open()) {
        return common::Status{common::Error::permission_denied, "Cannot open file"};
    }

    content.resize(static_cast<size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(size));
        if (static_cast<std::size_t>(file.gcount()) != size) {
            return common::Status{common::Error::integrity_error, "File size changed during read"};
        }
    }

    return common::Status{common::Error::none};
}

common::Status Std_FileRepository::write_file(const std::filesystem::path& virtual_path, const std::vector<std::uint8_t>& content) const {
    std::filesystem::path real_path;
    auto status = resolve_safe("/", virtual_path, real_path);
    if (status.error != common::Error::none) return status;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    std::filesystem::path temp_path = real_path;
    temp_path += ".tmp_" + std::to_string(dis(gen));

    std::ofstream file(temp_path, std::ios::binary);
    if (!file.is_open()) {
        return common::Status{common::Error::permission_denied, "Cannot create temp file"};
    }

    if (!content.empty()) {
        file.write(reinterpret_cast<const char*>(content.data()), static_cast<std::streamsize>(content.size()));
    }

    file.close();

    if (!file.good()) {
        std::filesystem::remove(temp_path);
        return common::Status{common::Error::integrity_error, "Write failed"};
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, real_path, ec);
    if (ec) {
        std::filesystem::copy_file(temp_path, real_path, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(temp_path);

        if (ec) {
            return common::Status{common::Error::integrity_error, "Atomic replace failed"};
        }
    }

    return common::Status{common::Error::none};
}

}