#include "hftp/filesystem/file_repository.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <system_error>

#include "hftp/integrity/sha256.h"

namespace hftp::filesystem {
namespace {

bool same_component(std::string left, std::string right) {
#ifdef _WIN32
    std::transform(left.begin(), left.end(), left.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::transform(right.begin(), right.end(), right.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
    return left == right;
}

bool is_subpath(const std::filesystem::path& root,
                const std::filesystem::path& candidate) {
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() ||
            !same_component(root_part->string(), candidate_part->string())) {
            return false;
        }
    }
    return true;
}

common::Status filesystem_error(common::Error error, std::string message,
                                const std::error_code& detail = {}) {
    if (detail) {
        message += ": " + detail.message();
    }
    return {error, std::move(message)};
}

std::atomic_uint64_t temporary_sequence{1};

} // namespace

common::Status normalize_virtual_path(const std::filesystem::path& current_directory,
                                      const std::filesystem::path& requested,
                                      std::filesystem::path& normalized) {
    normalized.clear();
    if (requested.empty()) {
        return {common::Error::invalid_argument, "Path is empty"};
    }
    if (requested.has_root_name()) {
        return {common::Error::permission_denied,
                "Native absolute paths are outside the FTP root"};
    }

    const auto contains_forbidden_character = [](const std::filesystem::path& path) {
        const auto text = path.generic_string();
        return std::any_of(text.begin(), text.end(), [](unsigned char value) {
            return value == 0 || value == '\r' || value == '\n';
        });
    };
    if (contains_forbidden_character(current_directory) ||
        contains_forbidden_character(requested)) {
        return {common::Error::invalid_argument,
                "Path contains a forbidden control character"};
    }

    std::vector<std::filesystem::path> components;
    const auto append_components = [&components](const std::filesystem::path& path,
                                                  bool reject_escape) -> common::Status {
        for (const auto& component : path) {
            if (component == path.root_name() || component == path.root_directory() ||
                component == "." || component.empty()) {
                continue;
            }
            if (component == "..") {
                if (components.empty()) {
                    if (reject_escape) {
                        return {common::Error::permission_denied,
                                "Path escapes the FTP root"};
                    }
                    continue;
                }
                components.pop_back();
                continue;
            }
            components.push_back(component);
        }
        return {};
    };

    if (!requested.has_root_directory()) {
        const auto current_status = append_components(current_directory, false);
        if (!current_status) {
            return current_status;
        }
    }
    const auto requested_status = append_components(requested, true);
    if (!requested_status) {
        return requested_status;
    }

    normalized = "/";
    for (const auto& component : components) {
        normalized /= component;
    }
    return {};
}

Std_FileRepository::Std_FileRepository(std::filesystem::path root_dir) {
    std::error_code error;
    if (!std::filesystem::is_directory(root_dir, error) || error) {
        throw std::invalid_argument("FTP root does not exist or is not a directory");
    }
    m_root_dir = std::filesystem::canonical(root_dir, error);
    if (error) {
        throw std::invalid_argument("Unable to canonicalize FTP root");
    }
}

common::Status Std_FileRepository::resolve_safe(
    const std::filesystem::path& current_directory,
    const std::filesystem::path& requested,
    std::filesystem::path& resolved) const {
    resolved.clear();
    std::filesystem::path virtual_path;
    const auto normalize_status =
        normalize_virtual_path(current_directory, requested, virtual_path);
    if (!normalize_status) {
        return normalize_status;
    }

    std::error_code error;
    const auto candidate = std::filesystem::weakly_canonical(
        m_root_dir / virtual_path.relative_path(), error);
    if (error) {
        return filesystem_error(common::Error::invalid_argument,
                                "Unable to resolve path", error);
    }
    if (!is_subpath(m_root_dir, candidate)) {
        return {common::Error::permission_denied, "Path escapes the FTP root"};
    }

    resolved = candidate;
    return {};
}

common::Status Std_FileRepository::list_file(
    const std::filesystem::path& virtual_path,
    std::vector<Entry>& entries) const {
    entries.clear();
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(physical_path, error) || error) {
        return {common::Error::not_found, "Directory not found"};
    }

    std::filesystem::directory_iterator iterator(physical_path, error);
    if (error) {
        return filesystem_error(common::Error::permission_denied,
                                "Unable to read directory", error);
    }
    for (const auto& directory_entry : iterator) {
        Entry entry;
        entry.name = directory_entry.path().filename();
        entry.directory = directory_entry.is_directory(error);
        if (error) {
            return filesystem_error(common::Error::integrity_error,
                                    "Unable to read entry type", error);
        }
        entry.size = entry.directory ? 0 : directory_entry.file_size(error);
        if (error && !entry.directory) {
            return filesystem_error(common::Error::integrity_error,
                                    "Unable to read entry size", error);
        }
        error.clear();
        entry.modified = directory_entry.last_write_time(error);
        if (error) {
            return filesystem_error(common::Error::integrity_error,
                                    "Unable to read entry timestamp", error);
        }
        entry.permissions = directory_entry.status(error).permissions();
        if (error) {
            return filesystem_error(common::Error::integrity_error,
                                    "Unable to read entry permissions", error);
        }
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& left, const Entry& right) {
                  return left.name.generic_string() < right.name.generic_string();
              });
    return {};
}

common::Status Std_FileRepository::read_file(
    const std::filesystem::path& virtual_path,
    std::vector<std::uint8_t>& content) const {
    content.clear();
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }

    std::error_code error;
    if (!std::filesystem::is_regular_file(physical_path, error) || error) {
        return {common::Error::not_found, "File not found"};
    }
    const auto size = std::filesystem::file_size(physical_path, error);
    if (error || size > static_cast<std::uintmax_t>(SIZE_MAX)) {
        return filesystem_error(common::Error::invalid_argument,
                                "File is too large", error);
    }

    std::ifstream input(physical_path, std::ios::binary);
    if (!input) {
        return {common::Error::permission_denied, "Unable to open file"};
    }
    content.resize(static_cast<std::size_t>(size));
    if (!content.empty()) {
        input.read(reinterpret_cast<char*>(content.data()),
                   static_cast<std::streamsize>(content.size()));
        if (static_cast<std::size_t>(input.gcount()) != content.size()) {
            content.clear();
            return {common::Error::integrity_error,
                    "File changed or could not be read completely"};
        }
    }
    return {};
}

common::Status Std_FileRepository::write_file(
    const std::filesystem::path& virtual_path,
    const std::vector<std::uint8_t>& content) const {
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(physical_path.parent_path(), error) || error) {
        return {common::Error::not_found, "Parent directory not found"};
    }
    if (std::filesystem::is_directory(physical_path, error)) {
        return {common::Error::invalid_argument, "Target path is a directory"};
    }
    error.clear();

    auto temporary = physical_path;
    temporary += ".hftp_tmp_" +
                 std::to_string(temporary_sequence.fetch_add(1));
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        return {common::Error::permission_denied, "Unable to create temporary file"};
    }
    if (!content.empty()) {
        output.write(reinterpret_cast<const char*>(content.data()),
                     static_cast<std::streamsize>(content.size()));
    }
    output.close();
    if (!output) {
        std::filesystem::remove(temporary, error);
        return {common::Error::integrity_error, "Unable to write complete file"};
    }

    std::filesystem::rename(temporary, physical_path, error);
    if (error) {
        error.clear();
        std::filesystem::copy_file(temporary, physical_path,
                                   std::filesystem::copy_options::overwrite_existing,
                                   error);
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
    }
    if (error) {
        return filesystem_error(common::Error::integrity_error,
                                "Unable to replace file", error);
    }
    return {};
}

common::Status Std_FileRepository::append_file(
    const std::filesystem::path& virtual_path,
    const std::vector<std::uint8_t>& content) const {
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(physical_path.parent_path(), error) || error) {
        return {common::Error::not_found, "Parent directory not found"};
    }
    if (std::filesystem::is_directory(physical_path, error)) {
        return {common::Error::invalid_argument, "Target path is a directory"};
    }

    std::ofstream output(physical_path, std::ios::binary | std::ios::app);
    if (!output) {
        return {common::Error::permission_denied, "Unable to append file"};
    }
    if (!content.empty()) {
        output.write(reinterpret_cast<const char*>(content.data()),
                     static_cast<std::streamsize>(content.size()));
    }
    return output ? common::Status{}
                  : common::Status{common::Error::integrity_error,
                                   "Unable to append complete content"};
}

common::Status Std_FileRepository::write_unique(
    const std::filesystem::path& virtual_directory,
    const std::vector<std::uint8_t>& content,
    std::filesystem::path& created_name) const {
    created_name.clear();
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path directory;
    const auto status = resolve_safe("/", virtual_directory, directory);
    if (!status) {
        return status;
    }
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error) || error) {
        return {common::Error::not_found, "Target directory not found"};
    }

    for (unsigned int attempt = 0; attempt < 1000; ++attempt) {
        const auto name = std::filesystem::path(
            "upload_" + std::to_string(temporary_sequence.fetch_add(1)) + ".bin");
        const auto physical_path = directory / name;
        if (std::filesystem::exists(physical_path, error)) {
            error.clear();
            continue;
        }
        std::ofstream output(physical_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            return {common::Error::permission_denied, "Unable to create unique file"};
        }
        if (!content.empty()) {
            output.write(reinterpret_cast<const char*>(content.data()),
                         static_cast<std::streamsize>(content.size()));
        }
        if (!output) {
            std::filesystem::remove(physical_path, error);
            return {common::Error::integrity_error,
                    "Unable to write unique file"};
        }
        created_name = name;
        return {};
    }
    return {common::Error::busy, "Unable to allocate a unique file name"};
}

common::Status Std_FileRepository::metadata(
    const std::filesystem::path& virtual_path,
    FileMetadata& result) const {
    result = {};
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }
    std::error_code error;
    const auto file_status = std::filesystem::status(physical_path, error);
    if (error || !std::filesystem::exists(file_status)) {
        return {common::Error::not_found, "File or directory not found"};
    }
    result.directory = std::filesystem::is_directory(file_status);
    result.permissions = file_status.permissions();
    result.size = result.directory ? 0 : std::filesystem::file_size(physical_path, error);
    if (error) {
        return filesystem_error(common::Error::integrity_error,
                                "Unable to read file size", error);
    }
    result.modified = std::filesystem::last_write_time(physical_path, error);
    if (error) {
        return filesystem_error(common::Error::integrity_error,
                                "Unable to read modification time", error);
    }
    return {};
}

common::Status Std_FileRepository::make_directory(
    const std::filesystem::path& virtual_path) const {
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }
    std::error_code error;
    if (std::filesystem::exists(physical_path, error)) {
        return {common::Error::busy, "Path already exists"};
    }
    if (!std::filesystem::is_directory(physical_path.parent_path(), error) || error) {
        return {common::Error::not_found, "Parent directory not found"};
    }
    if (!std::filesystem::create_directory(physical_path, error) || error) {
        return filesystem_error(common::Error::permission_denied,
                                "Unable to create directory", error);
    }
    return {};
}

common::Status Std_FileRepository::remove_directory(
    const std::filesystem::path& virtual_path) const {
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }
    if (physical_path == m_root_dir) {
        return {common::Error::permission_denied, "Cannot remove the FTP root"};
    }
    std::error_code error;
    if (!std::filesystem::is_directory(physical_path, error) || error) {
        return {common::Error::not_found, "Directory not found"};
    }
    if (!std::filesystem::is_empty(physical_path, error)) {
        return {common::Error::busy, "Directory is not empty"};
    }
    if (error || !std::filesystem::remove(physical_path, error)) {
        return filesystem_error(common::Error::permission_denied,
                                "Unable to remove directory", error);
    }
    return {};
}

common::Status Std_FileRepository::remove_file(
    const std::filesystem::path& virtual_path) const {
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(physical_path, error) || error) {
        return {common::Error::not_found, "File not found"};
    }
    if (!std::filesystem::remove(physical_path, error) || error) {
        return filesystem_error(common::Error::permission_denied,
                                "Unable to remove file", error);
    }
    return {};
}

common::Status Std_FileRepository::rename_entry(
    const std::filesystem::path& from,
    const std::filesystem::path& to) const {
    const std::scoped_lock lock(m_mutation_mutex);
    std::filesystem::path physical_from;
    auto status = resolve_safe("/", from, physical_from);
    if (!status) {
        return status;
    }
    std::filesystem::path physical_to;
    status = resolve_safe("/", to, physical_to);
    if (!status) {
        return status;
    }
    std::error_code error;
    if (!std::filesystem::exists(physical_from, error) || error) {
        return {common::Error::not_found, "Source path not found"};
    }
    if (std::filesystem::exists(physical_to, error)) {
        return {common::Error::busy, "Target path already exists"};
    }
    if (!std::filesystem::is_directory(physical_to.parent_path(), error) || error) {
        return {common::Error::not_found, "Target directory not found"};
    }
    std::filesystem::rename(physical_from, physical_to, error);
    if (error) {
        return filesystem_error(common::Error::permission_denied,
                                "Unable to rename path", error);
    }
    return {};
}

common::Status Std_FileRepository::sha256_file(
    const std::filesystem::path& virtual_path,
    std::string& hexadecimal_digest) const {
    hexadecimal_digest.clear();
    std::filesystem::path physical_path;
    const auto status = resolve_safe("/", virtual_path, physical_path);
    if (!status) {
        return status;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(physical_path, error) || error) {
        return {common::Error::not_found, "File not found"};
    }

    std::ifstream input(physical_path, std::ios::binary);
    if (!input) {
        return {common::Error::permission_denied, "Unable to open file"};
    }
    integrity::Sha256 hash;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0) {
            hash.update(std::span<const std::uint8_t>(
                buffer.data(), static_cast<std::size_t>(count)));
        }
    }
    if (!input.eof()) {
        return {common::Error::integrity_error, "Unable to hash complete file"};
    }
    const auto digest = hash.finish();
    hexadecimal_digest = integrity::to_hex(digest);
    return {};
}

} // namespace hftp::filesystem
