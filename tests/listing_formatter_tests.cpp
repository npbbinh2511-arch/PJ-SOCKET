#include "hftp/filesystem/listing_formatter.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__ \
                      << " -> " #condition "\n"; \
            return 1; \
        } \
    } while (false)

int main() {
    using hftp::filesystem::Entry;
    using perms = std::filesystem::perms;

    const auto timestamp = std::filesystem::file_time_type::clock::now();
    const std::vector<Entry> entries{
        {"file\nname.bin", false, 42, timestamp,
         perms::owner_read | perms::owner_write | perms::group_read | perms::others_read},
        {"folder", true, 0, timestamp,
         perms::owner_all | perms::group_read | perms::group_exec |
             perms::others_read | perms::others_exec},
    };

    const auto names = hftp::filesystem::format_name_listing(entries);
    TEST_CHECK(names == "file_name.bin\r\nfolder\r\n");

    const auto detailed = hftp::filesystem::format_detailed_listing(entries);
    TEST_CHECK(detailed.find("-rw-r--r--") != std::string::npos);
    TEST_CHECK(detailed.find("drwxr-xr-x") != std::string::npos);
    TEST_CHECK(detailed.find("          42") != std::string::npos);
    TEST_CHECK(detailed.find("file_name.bin\r\n") != std::string::npos);
    TEST_CHECK(hftp::filesystem::format_ftp_timestamp(timestamp).size() == 14);

    std::cout << "==> ALL LIST/NLST/MDTM FORMATTER TESTS PASSED! <==\n";
    return 0;
}
