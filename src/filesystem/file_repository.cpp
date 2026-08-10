#include "hftp/filesystem/file_repository.hpp"
#include <fstream>
#include <iostream>

namespace hftp::filesystem {

Std_FileRepository::Std_FileRepository(std::filesystem::path root_dir) : m_root_dir(root_dir) {
    std::error_code ec;
    if (std::filesystem::exists(m_root_dir, ec)) {
        m_root_dir = std::filesystem::canonical(m_root_dir, ec);
    }
}

common::Status Std_FileRepository::resolve_safe(const std::filesystem::path& cw_dir, const std::filesystem::path& requested, std::filesystem::path& resolved) const {
    try {
        std::filesystem::path combined = m_root_dir / cw_dir / requested;

        std::filesystem::path normalized = std::filesystem::weakly_canonical(combined);

        std::string root_str = m_root_dir.string();
        std::string norm_str = normalized.string();

        if (norm_str.rfind(root_str, 0) != 0) {
            std::cerr << "[SECURITY] Path traversal detected: " << requested << "\n";
            return common::Status{common::Error::permission_denied, "Access denied"};
        }

        resolved = normalized;
        return common::Status{common::Error::none};
    } catch (...) {
        return common::Status{common::Error::invalid_argument, "Invalid path resolution"};
    }
}

common::Status Std_FileRepository::list_file(const std::filesystem::path& path, std::vector<Entry>& entries) const {
    entries.clear();
    std::error_code ec;

    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_directory(path, ec)) {
        return common::Status{common::Error::not_found, "Directory not found"};
    }

    for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
        if (ec) break;
        Entry item;
        item.name = entry.path().filename();
        item.directory = entry.is_directory(ec);
        item.size = item.directory ? 0 : entry.file_size(ec);
        entries.push_back(item);
    }

    return common::Status{common::Error::none};
}

common::Status Std_FileRepository::read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& content) const {
    std::error_code ec;

    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        return common::Status{common::Error::not_found, "File not found"};
    }

    std::ifstream file(path, std::ios::binary);

    if (!file.is_open()) {
        return common::Status{common::Error::permission_denied, "Cannot open file"};
    }

    auto file_size = std::filesystem::file_size(path, ec);
    content.resize(file_size);
    file.read(reinterpret_cast<char*>(content.data()), file_size);

    if (file.good()) {
        return common::Status{common::Error::none};
    }

    return common::Status{common::Error::integrity_error, "Read file failed"};
}

common::Status Std_FileRepository::write_file(const std::filesystem::path& path, std::vector<std::uint8_t>& content) const {
    std::filesystem::path temp_path = path;
    temp_path += ".tmp_" + std::to_string(std::rand());
    std::ofstream file(temp_path, std::ios::binary);

    
    if (!file.is_open()) {
        return common::Status{common::Error::permission_denied, "Cannot create temp file"};
    }

    if (!content.empty()) {
        file.write(reinterpret_cast<const char*>(content.data()), content.size());
    }

    file.close();

    if (!file.good()) {
        std::filesystem::remove(temp_path); 
        return common::Status{common::Error::integrity_error, "Write failed"};
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, path, ec);
    if (ec) {
        std::filesystem::remove(temp_path);
        return common::Status{common::Error::integrity_error, "Atomic rename failed"};
    }

    return common::Status{common::Error::none};
    }

}