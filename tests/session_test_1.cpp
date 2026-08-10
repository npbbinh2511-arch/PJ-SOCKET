#include <filesystem>
#include <iostream>
#include "hftp/filesystem/file_repository.h"
#include "hftp/session/session.h"
#include "hftp/session/session_service.h"

namespace fs = std::filesystem;
using namespace hftp::filesystem;
using namespace hftp::session;
using namespace hftp::common;

#define TEST_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__ << " -> Condition failed: " #cond << "\n"; \
            return false; \
        } \
    } while (0)

bool test_session_cwd_and_sandbox() {
    fs::path test_root = fs::temp_directory_path() / "hftp_test_session";
    fs::remove_all(test_root);
    fs::create_directories(test_root / "docs");
    fs::create_directories(test_root / "downloads");

    Std_FileRepository repo(test_root);
    SessionService service(repo);
    Session session;
    session.id = 1;

    // 1. Kiểm tra ban đầu CWD = "/"
    TEST_CHECK(service.get_pwd(session) == "/");

    // 2. CWD vào thư mục con "docs"
    TEST_CHECK(service.change_directory(session, "docs").error == Error::none);
    TEST_CHECK(service.get_pwd(session) == "/docs");

    // 3. CWD lùi lại "/"
    TEST_CHECK(service.change_directory(session, "..").error == Error::none);
    TEST_CHECK(service.get_pwd(session) == "/");

    // 4. Test Sandbox Protection: Thử CWD ra ngoài sandbox
    TEST_CHECK(service.change_directory(session, "../../etc").error == Error::permission_denied);
    TEST_CHECK(service.get_pwd(session) == "/"); // CWD giữ nguyên không bị hỏng

    // 5. Test CWD vào thư mục không tồn tại
    TEST_CHECK(service.change_directory(session, "non_existent_folder").error == Error::not_found);

    fs::remove_all(test_root);
    std::cout << "[PASS] Test Session CWD & Sandbox Security successfully!\n";
    return true;
}

int main() {
    if (!test_session_cwd_and_sandbox()) {
        std::cerr << "\n==> SESSION TESTS FAILED! <==\n";
        return 1;
    }
    std::cout << "\n==> ALL SESSION TESTS PASSED! <==\n";
    return 0;
}