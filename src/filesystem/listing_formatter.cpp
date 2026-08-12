#include "hftp/filesystem/listing_formatter.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace hftp::filesystem {

namespace {

std::string safe_name(const std::filesystem::path& name) {
    auto value = name.generic_string();
    for (char& character : value) {
        if (character == '\r' || character == '\n') {
            character = '_';
        }
    }
    return value;
}

bool has_permission(std::filesystem::perms value, std::filesystem::perms bit) {
    return (value & bit) != std::filesystem::perms::none;
}

std::string permission_text(const Entry& entry) {
    using perms = std::filesystem::perms;
    std::string text(entry.directory ? "d" : "-");
    const struct PermissionCharacter {
        perms bit;
        char character;
    } permissions[]{
        {perms::owner_read, 'r'}, {perms::owner_write, 'w'}, {perms::owner_exec, 'x'},
        {perms::group_read, 'r'}, {perms::group_write, 'w'}, {perms::group_exec, 'x'},
        {perms::others_read, 'r'}, {perms::others_write, 'w'}, {perms::others_exec, 'x'},
    };
    for (const auto& permission : permissions) {
        text.push_back(has_permission(entry.permissions, permission.bit)
                           ? permission.character
                           : '-');
    }
    return text;
}

} // namespace

std::string format_ftp_timestamp(std::filesystem::file_time_type modified) {
    using namespace std::chrono;
    const auto system_time = time_point_cast<system_clock::duration>(
        modified - std::filesystem::file_time_type::clock::now() + system_clock::now());
    const std::time_t time = system_clock::to_time_t(system_time);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream output;
    output << std::put_time(&utc, "%Y%m%d%H%M%S");
    return output.str();
}

std::string format_name_listing(const std::vector<Entry>& entries) {
    std::string output;
    for (const auto& entry : entries) {
        output += safe_name(entry.name);
        output += "\r\n";
    }
    return output;
}

std::string format_detailed_listing(const std::vector<Entry>& entries) {
    std::ostringstream output;
    for (const auto& entry : entries) {
        output << permission_text(entry) << ' '
               << std::setw(12) << entry.size << ' '
               << format_ftp_timestamp(entry.modified) << ' '
               << safe_name(entry.name) << "\r\n";
    }
    return output.str();
}

} // namespace hftp::filesystem
