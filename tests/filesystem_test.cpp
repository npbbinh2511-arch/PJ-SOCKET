#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>
#include "hftp/filesystem/file_repository.h"

namespace fs = std::filesystem;
using namespace hftp::filesystem;
using namespace hftp::common;

// Helper macro thay thế assert(): kiểm tra điều kiện, nếu sai sẽ báo lỗi và trả về false
#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> Condition failed: " #cond << "\n"; \
            return false; \
        } \
    } while (0)

bool test_sandbox_security() {
    fs::path test_root = fs::temp_directory_path() / "hftp_test_sandbox";
    fs::create_directories(test_root / "subdir");

    Std_FileRepository repo(test_root);
    fs::path resolved;

    TEST_CHECK(repo.resolve_safe("/", "subdir", resolved).error == Error::none);
    TEST_CHECK(repo.resolve_safe("/", "../../../etc/passwd", resolved).error == Error::permission_denied);
    TEST_CHECK(repo.resolve_safe("/", "../hftp_test_sandbox_evil", resolved).error == Error::permission_denied);

    fs::remove_all(test_root);
    std::cout << "[PASS] Test Sandbox Security successfully!\n";
    return true;
}

bool test_file_operations_and_edge_cases() {
    fs::path test_root = fs::temp_directory_path() / "hftp_test_ops";
    fs::remove_all(test_root); // Dọn dẹp trước khi test
    fs::create_directories(test_root);

    Std_FileRepository repo(test_root);

    // 1. Test ghi & đọc file thông thường
    std::vector<uint8_t> write_data = {'H', 'E', 'L', 'L', 'O'};
    TEST_CHECK(repo.write_file("test.txt", write_data).error == Error::none);

    std::vector<uint8_t> read_data;
    TEST_CHECK(repo.read_file("test.txt", read_data).error == Error::none);
    TEST_CHECK(read_data == write_data);

    // 2. Test ghi đè file đã tồn tại (Overwrite)
    std::vector<uint8_t> new_data = {'W', 'O', 'R', 'L', 'D'};
    TEST_CHECK(repo.write_file("test.txt", new_data).error == Error::none);
    TEST_CHECK(repo.read_file("test.txt", read_data).error == Error::none);
    TEST_CHECK(read_data == new_data);

    // 3. Test file rỗng (0 bytes)
    std::vector<uint8_t> empty_data;
    TEST_CHECK(repo.write_file("empty.txt", empty_data).error == Error::none);
    TEST_CHECK(repo.read_file("empty.txt", read_data).error == Error::none);
    TEST_CHECK(read_data.empty());

    // 4. Test file binary chứa byte 0x00
    std::vector<uint8_t> binary_data = {0x00, 0x01, 0x00, 0xFF, 0x00, 0xFE};
    TEST_CHECK(repo.write_file("binary.bin", binary_data).error == Error::none);
    TEST_CHECK(repo.read_file("binary.bin", read_data).error == Error::none);
    TEST_CHECK(read_data == binary_data);

    // 5. Test danh sách file
    std::vector<Entry> entries;
    TEST_CHECK(repo.list_file("/", entries).error == Error::none);
    TEST_CHECK(entries.size() == 3); // test.txt, empty.txt, binary.bin

    fs::remove_all(test_root);
    std::cout << "[PASS] Test File Operations & Edge Cases successfully!\n";
    return true;
}

int main() {
    bool all_passed = true;

    all_passed &= test_sandbox_security();
    all_passed &= test_file_operations_and_edge_cases();

    if (!all_passed) {
        std::cerr << "\n==> SOME TESTS FAILED! <==\n";
        return 1;
    }

    std::cout << "\n==> ALL FILESYSTEM TESTS PASSED! <==\n";
    return 0;
}