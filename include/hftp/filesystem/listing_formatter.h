#ifndef HFTP_FILESYSTEM_LISTING_FORMATTER_H
#define HFTP_FILESYSTEM_LISTING_FORMATTER_H

#include <filesystem>
#include <string>
#include <vector>

#include "hftp/filesystem/file_repository.h"

namespace hftp::filesystem {

[[nodiscard]] std::string format_ftp_timestamp(
    std::filesystem::file_time_type modified);
[[nodiscard]] std::string format_name_listing(const std::vector<Entry>& entries);
[[nodiscard]] std::string format_detailed_listing(const std::vector<Entry>& entries);

} // namespace hftp::filesystem

#endif // HFTP_FILESYSTEM_LISTING_FORMATTER_H
