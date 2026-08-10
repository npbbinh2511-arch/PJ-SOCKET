#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "hftp/filesystem/file_repository.h"

namespace fs = std::filesystem;
using namespace hftp::filesystem;
using namespace hftp::common;

void test_sandbox_security() {
    fs::path test_root = fs::temp_directory_path() / "hftp_test_sandbox";
    fs::create_directories(test_root / "subdir");

    Std_FileRepository repo(test_root);
    fs::path resolved;

    assert(repo.resolve_safe("/", "subdir", resolved).error == Error::none);

    assert(repo.resolve_safe("/", "../../../etc/passwd", resolved).error == Error::permission_denied);

    assert(repo.resolve_safe("/", "../hftp_test_sandbox_evil", resolved).error == Error::permission_denied);

    fs::remove_all(test_root);
    std::cout << "[PASS] Test Sandbox Security successfully!\n";
}

void test_file_operations() {
    fs::path test_root = fs::temp_directory_path() / "hftp_test_ops";
    fs::create_directories(test_root);

    Std_FileRepository repo(test_root);

    std::vector<uint8_t> write_data = {'H', 'E', 'L', 'L', 'O'};
    assert(repo.write_file("test.txt", write_data).error == Error::none);

    std::vector<uint8_t> read_data;
    assert(repo.read_file("test.txt", read_data).error == Error::none);
    assert(read_data == write_data);

    std::vector<Entry> entries;
    assert(repo.list_file("/", entries).error == Error::none);
    assert(entries.size() == 1);
    assert(entries[0].name == "test.txt");

    fs::remove_all(test_root);
    std::cout << "[PASS] Test File Operations successfully!\n";
}

int main() {
    try {
        test_sandbox_security();
        test_file_operations();
        std::cout << "\n==> ALL FILESYSTEM TESTS PASSED! <==\n";
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << "\n";
        return 1;
    }
    return 0;
}