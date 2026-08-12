#include "hftp/filesystem/file_repository.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using hftp::common::Error;
using hftp::filesystem::Entry;
using hftp::filesystem::FileMetadata;
using hftp::filesystem::Std_FileRepository;

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "  [FAIL] " << __FILE__ << ':' << __LINE__ \
                      << " -> " #condition "\n"; \
            return false; \
        } \
    } while (false)

class TempDirectory {
public:
    explicit TempDirectory(const std::string& label) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = fs::temp_directory_path() /
                 ("hftp_" + label + "_" + std::to_string(unique));

        std::error_code ec;
        fs::create_directories(m_path, ec);
        if (ec) {
            throw std::runtime_error("Cannot create test directory: " + ec.message());
        }
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(m_path, ec);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const fs::path& path() const noexcept { return m_path; }

private:
    fs::path m_path;
};

bool test_constructor_validation() {
    TempDirectory temporary("constructor");

    bool missing_root_rejected = false;
    try {
        const Std_FileRepository repository(temporary.path() / "missing");
    } catch (const std::invalid_argument&) {
        missing_root_rejected = true;
    }
    TEST_CHECK(missing_root_rejected);

    const auto regular_file = temporary.path() / "root.txt";
    std::ofstream(regular_file) << "not a directory";

    bool file_root_rejected = false;
    try {
        const Std_FileRepository repository(regular_file);
    } catch (const std::invalid_argument&) {
        file_root_rejected = true;
    }
    TEST_CHECK(file_root_rejected);

    std::cout << "[PASS] Constructor validation\n";
    return true;
}

bool test_sandbox_security() {
    TempDirectory sandbox("sandbox");
    fs::create_directories(sandbox.path() / "parent" / "child");

    const Std_FileRepository repository(sandbox.path());
    fs::path resolved;

    TEST_CHECK(repository.resolve_safe("/", "parent/child", resolved).error == Error::none);
    TEST_CHECK(resolved == fs::canonical(sandbox.path() / "parent" / "child"));

    TEST_CHECK(repository.resolve_safe("/parent/child", "..", resolved).error == Error::none);
    TEST_CHECK(resolved == fs::canonical(sandbox.path() / "parent"));

    TEST_CHECK(repository.resolve_safe("/parent", "/parent/child", resolved).error == Error::none);
    TEST_CHECK(resolved == fs::canonical(sandbox.path() / "parent" / "child"));

    TEST_CHECK(repository.resolve_safe("/", "../outside", resolved).error == Error::permission_denied);
    TEST_CHECK(repository.resolve_safe("/", "../../../etc/passwd", resolved).error == Error::permission_denied);
    TEST_CHECK(repository.resolve_safe("/parent", "../../outside", resolved).error == Error::permission_denied);
    TEST_CHECK(resolved.empty());

    const std::string embedded_nul("parent\0hidden", 13);
    TEST_CHECK(repository.resolve_safe("/", embedded_nul, resolved).error ==
               Error::invalid_argument);
    TEST_CHECK(repository.resolve_safe("/", "forged\r\nname", resolved).error ==
               Error::invalid_argument);

#if defined(_WIN32)
    TEST_CHECK(repository.resolve_safe("/", fs::temp_directory_path(), resolved).error == Error::permission_denied);
#endif

    TempDirectory outside("outside");
    std::error_code link_error;
    fs::create_directory_symlink(outside.path(), sandbox.path() / "escape_link", link_error);
    if (!link_error) {
        TEST_CHECK(repository.resolve_safe("/", "escape_link/file.bin", resolved).error == Error::permission_denied);
    } else {
        std::cout << "[SKIP] Symlink escape test: OS did not permit symlink creation\n";
    }

    std::cout << "[PASS] Sandbox and path traversal protection\n";
    return true;
}

bool test_binary_read_write_and_overwrite() {
    TempDirectory storage("binary_io");
    fs::create_directories(storage.path() / "nested");
    const Std_FileRepository repository(storage.path());

    const std::vector<std::uint8_t> first_data{'H', 'E', 'L', 'L', 'O'};
    TEST_CHECK(repository.write_file("test.txt", first_data).error == Error::none);

    std::vector<std::uint8_t> read_data;
    TEST_CHECK(repository.read_file("test.txt", read_data).error == Error::none);
    TEST_CHECK(read_data == first_data);

    const std::vector<std::uint8_t> replacement{'W', 'O', 'R', 'L', 'D', '!'};
    TEST_CHECK(repository.write_file("test.txt", replacement).error == Error::none);
    TEST_CHECK(repository.read_file("test.txt", read_data).error == Error::none);
    TEST_CHECK(read_data == replacement);

    const std::vector<std::uint8_t> binary_data{0x00, 0x01, 0x00, 0x7F, 0x80, 0xFF};
    TEST_CHECK(repository.write_file("nested/binary.bin", binary_data).error == Error::none);
    TEST_CHECK(repository.read_file("nested/binary.bin", read_data).error == Error::none);
    TEST_CHECK(read_data == binary_data);

    const std::vector<std::uint8_t> empty_data;
    TEST_CHECK(repository.write_file("empty.bin", empty_data).error == Error::none);
    TEST_CHECK(repository.read_file("empty.bin", read_data).error == Error::none);
    TEST_CHECK(read_data.empty());

    std::cout << "[PASS] Binary read, write, empty file and overwrite\n";
    return true;
}

bool test_directory_listing() {
    TempDirectory storage("listing");
    fs::create_directories(storage.path() / "folder");
    const Std_FileRepository repository(storage.path());

    TEST_CHECK(repository.write_file("b.bin", {0x00, 0x01, 0x02}).error == Error::none);
    TEST_CHECK(repository.write_file("a.txt", {'A'}).error == Error::none);

    std::vector<Entry> entries;
    TEST_CHECK(repository.list_file("/", entries).error == Error::none);
    TEST_CHECK(entries.size() == 3);
    TEST_CHECK(entries[0].name == "a.txt");
    TEST_CHECK(!entries[0].directory && entries[0].size == 1);
    TEST_CHECK(entries[1].name == "b.bin");
    TEST_CHECK(!entries[1].directory && entries[1].size == 3);
    TEST_CHECK(entries[2].name == "folder");
    TEST_CHECK(entries[2].directory && entries[2].size == 0);

    std::cout << "[PASS] Sorted directory listing and metadata\n";
    return true;
}

bool test_error_paths_and_cleanup() {
    TempDirectory storage("errors");
    fs::create_directories(storage.path() / "directory");
    const Std_FileRepository repository(storage.path());

    std::vector<std::uint8_t> stale_data{1, 2, 3};
    TEST_CHECK(repository.read_file("missing.bin", stale_data).error == Error::not_found);
    TEST_CHECK(stale_data.empty());

    std::vector<Entry> entries{{"stale", false, 99}};
    TEST_CHECK(repository.list_file("missing", entries).error == Error::not_found);
    TEST_CHECK(entries.empty());

    TEST_CHECK(repository.list_file("directory/../missing", entries).error == Error::not_found);
    TEST_CHECK(repository.write_file("missing/file.bin", {1}).error == Error::not_found);
    TEST_CHECK(repository.write_file("directory", {1}).error == Error::invalid_argument);
    TEST_CHECK(repository.write_file("../escape.bin", {1}).error == Error::permission_denied);
    TEST_CHECK(!fs::exists(storage.path().parent_path() / "escape.bin"));

    for (const auto& entry : fs::directory_iterator(storage.path())) {
        TEST_CHECK(entry.path().filename().string().find(".hftp_tmp_") == std::string::npos);
    }

    std::cout << "[PASS] Error mapping, output cleanup and temporary-file cleanup\n";
    return true;
}

bool test_metadata_mutations_append_unique_and_hash() {
    TempDirectory storage("metadata_mutations");
    const Std_FileRepository repository(storage.path());

    TEST_CHECK(repository.make_directory("documents").error == Error::none);
    TEST_CHECK(repository.make_directory("documents").error == Error::busy);

    FileMetadata directory_metadata;
    TEST_CHECK(repository.metadata("documents", directory_metadata).error == Error::none);
    TEST_CHECK(directory_metadata.directory);
    TEST_CHECK(directory_metadata.size == 0);

    TEST_CHECK(repository.write_file("documents/data.bin", {'a', 'b', 'c'}).error == Error::none);
    TEST_CHECK(repository.append_file("documents/data.bin", {'d', 'e', 'f'}).error == Error::none);

    FileMetadata file_metadata;
    TEST_CHECK(repository.metadata("documents/data.bin", file_metadata).error == Error::none);
    TEST_CHECK(!file_metadata.directory);
    TEST_CHECK(file_metadata.size == 6);

    std::string digest;
    TEST_CHECK(repository.sha256_file("documents/data.bin", digest).error == Error::none);
    TEST_CHECK(digest == "bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721");

    TEST_CHECK(repository.rename_entry("documents/data.bin", "documents/renamed.bin").error == Error::none);
    TEST_CHECK(repository.rename_entry("documents/renamed.bin", "../escape.bin").error == Error::permission_denied);
    TEST_CHECK(repository.remove_directory("documents").error == Error::busy);
    TEST_CHECK(repository.remove_file("documents/renamed.bin").error == Error::none);

    std::filesystem::path unique_name;
    const std::vector<std::uint8_t> unique_content{0x00, 0x10, 0x20, 0xFF};
    TEST_CHECK(repository.write_unique("documents", unique_content, unique_name).error == Error::none);
    TEST_CHECK(!unique_name.empty());

    std::vector<std::uint8_t> read_back;
    TEST_CHECK(repository.read_file(std::filesystem::path{"documents"} / unique_name, read_back).error == Error::none);
    TEST_CHECK(read_back == unique_content);
    TEST_CHECK(repository.remove_file(std::filesystem::path{"documents"} / unique_name).error == Error::none);
    TEST_CHECK(repository.remove_directory("documents").error == Error::none);
    TEST_CHECK(repository.remove_directory("/").error == Error::permission_denied);

    TEST_CHECK(repository.metadata("missing", file_metadata).error == Error::not_found);
    TEST_CHECK(repository.sha256_file("missing", digest).error == Error::not_found);
    TEST_CHECK(digest.empty());

    std::cout << "[PASS] Metadata, MKD/RMD, DELE, rename, append, STOU and SHA-256\n";
    return true;
}

int main() {
    const std::vector<bool (*)()> tests{
        test_constructor_validation,
        test_sandbox_security,
        test_binary_read_write_and_overwrite,
        test_directory_listing,
        test_error_paths_and_cleanup,
        test_metadata_mutations_append_unique_and_hash,
    };

    bool all_passed = true;
    for (const auto test : tests) {
        all_passed = test() && all_passed;
    }

    if (!all_passed) {
        std::cerr << "\n==> SOME FILESYSTEM TESTS FAILED! <==\n";
        return 1;
    }

    std::cout << "\n==> ALL FILESYSTEM TESTS PASSED! <==\n";
    return 0;
}
