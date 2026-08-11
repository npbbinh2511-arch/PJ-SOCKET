#include "hftp/filesystem/file_repository.h"
#include "hftp/session/session_service.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using hftp::common::Error;

#define TEST_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "[FAIL] " << __FILE__ << ':' << __LINE__ \
                      << " -> " #condition "\n"; \
            return false; \
        } \
    } while (false)

namespace {

class TempDirectory {
public:
    TempDirectory() {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("hftp_member_c_" + std::to_string(unique));
        std::error_code ec;
        fs::create_directories(path_, ec);
        if (ec) {
            throw std::runtime_error("Cannot create member C integration directory");
        }
    }

    ~TempDirectory() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }

    [[nodiscard]] const fs::path& path() const noexcept { return path_; }

private:
    fs::path path_;
};

bool test_session_path_isolation() {
    TempDirectory root;
    fs::create_directories(root.path() / "alice" / "docs");
    fs::create_directories(root.path() / "bob");

    hftp::filesystem::Std_FileRepository repository(root.path());
    hftp::session::SessionService sessions(repository);
    hftp::session::Session alice;
    hftp::session::Session bob;

    TEST_CHECK(sessions.change_directory(alice, "/alice/docs").error == Error::none);
    TEST_CHECK(sessions.change_directory(bob, "/bob").error == Error::none);
    TEST_CHECK(sessions.get_pwd(alice).generic_string() == "/alice/docs");
    TEST_CHECK(sessions.get_pwd(bob).generic_string() == "/bob");

    TEST_CHECK(sessions.change_to_parent_directory(alice).error == Error::none);
    TEST_CHECK(sessions.get_pwd(alice).generic_string() == "/alice");
    TEST_CHECK(sessions.get_pwd(bob).generic_string() == "/bob");
    TEST_CHECK(sessions.change_directory(bob, "../../outside").error == Error::permission_denied);
    TEST_CHECK(sessions.get_pwd(bob).generic_string() == "/bob");

    std::cout << "[PASS] Stage 2 path support and per-session CWD isolation\n";
    return true;
}

bool test_hybrid_vertical_slice_repository_boundary() {
    TempDirectory root;
    hftp::filesystem::Std_FileRepository repository(root.path());

    // Mo phong payload ma RDT cua A giao cho C sau STOR TYPE I.
    std::vector<std::uint8_t> uploaded(128 * 1024);
    for (std::size_t index = 0; index < uploaded.size(); ++index) {
        uploaded[index] = static_cast<std::uint8_t>((index * 37U + 11U) & 0xFFU);
    }
    uploaded[0] = 0;
    uploaded[uploaded.size() / 2] = 0;
    uploaded.back() = 0xFF;

    TEST_CHECK(repository.write_file("uploads/payload.bin", uploaded).error == Error::not_found);
    TEST_CHECK(repository.make_directory("uploads").error == Error::none);
    TEST_CHECK(repository.write_file("uploads/payload.bin", uploaded).error == Error::none);

    std::string hash_after_stor;
    TEST_CHECK(repository.sha256_file("uploads/payload.bin", hash_after_stor).error == Error::none);
    TEST_CHECK(hash_after_stor.size() == 64);

    // Mo phong RETR: B/A lay byte tu repository va gui lai qua data channel.
    std::vector<std::uint8_t> downloaded;
    TEST_CHECK(repository.read_file("uploads/payload.bin", downloaded).error == Error::none);
    TEST_CHECK(downloaded == uploaded);

    std::string hash_after_retr;
    TEST_CHECK(repository.sha256_file("uploads/payload.bin", hash_after_retr).error == Error::none);
    TEST_CHECK(hash_after_retr == hash_after_stor);
    TEST_CHECK(repository.write_file("../escaped.bin", uploaded).error == Error::permission_denied);

    std::cout << "[PASS] Stage 4 STOR/RETR filesystem boundary, binary equality and hash\n";
    return true;
}

} // namespace

int main() {
    if (!test_session_path_isolation() || !test_hybrid_vertical_slice_repository_boundary()) {
        return 1;
    }
    std::cout << "==> ALL MEMBER C INTEGRATION TESTS PASSED! <==\n";
    return 0;
}
