#include <filesystem>
#include <iostream>
#include "hftp/filesystem/file_repository.h"
#include "hftp/session/session.h"
#include "hftp/session/session_service.h"
#include "hftp/protocol/command_dispatcher.h"

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
    std::error_code ec;
    fs::remove_all(test_root, ec);
    fs::create_directories(test_root / "docs", ec);
    fs::create_directories(test_root / "downloads", ec);

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

    fs::remove_all(test_root, ec);
    std::cout << "[PASS] Test Session CWD & Sandbox Security successfully!\n";
    return true;
}

// Sửa kiểu trả về từ void sang bool để tương thích với TEST_CHECK
bool test_command_dispatcher_integration() {
    // 1. Tạo repository tạm
    std::filesystem::path test_root = std::filesystem::temp_directory_path() / "hftp_dispatcher_test";
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);
    std::filesystem::create_directories(test_root / "subdir", ec);

    // 2. Khởi tạo FileRepository và SessionService
    hftp::filesystem::Std_FileRepository repo(test_root);
    hftp::session::SessionService service(repo);

    // 3. Khởi tạo CommandDispatcher
    hftp::protocol::CommandDispatcher dispatcher(service);
    hftp::session::Session session;

    // 4. Test lệnh PWD
    hftp::protocol::Command pwd_cmd{"PWD", ""};
    std::string res_pwd = dispatcher.dispatch(session, pwd_cmd);
    TEST_CHECK(res_pwd.rfind("257", 0) == 0); // Bắt đầu bằng mã 257

    // 5. Test lệnh CWD
    hftp::protocol::Command cwd_cmd{"CWD", "subdir"};
    std::string res_cwd = dispatcher.dispatch(session, cwd_cmd);
    TEST_CHECK(res_cwd.rfind("250", 0) == 0); // Bắt đầu bằng mã 250

    // 6. Test lệnh CDUP
    hftp::protocol::Command cdup_cmd{"CDUP", ""};
    std::string res_cdup = dispatcher.dispatch(session, cdup_cmd);
    TEST_CHECK(res_cdup.rfind("200", 0) == 0 || res_cdup.rfind("250", 0) == 0); // Mã 200 hoặc 250 thành công

    // 7. Dọn dẹp thư mục tạm
    std::filesystem::remove_all(test_root, ec);
    std::cout << "[PASS] Test Command Dispatcher Integration successfully!\n";
    return true;
}

int main() {
    if (!test_session_cwd_and_sandbox()) {
        std::cerr << "\n==> SESSION CWD TESTS FAILED! <==\n";
        return 1;
    }

    if (!test_command_dispatcher_integration()) {
        std::cerr << "\n==> DISPATCHER INTEGRATION TESTS FAILED! <==\n";
        return 1;
    }

    std::cout << "\n==> ALL SESSION & DISPATCHER TESTS PASSED! <==\n";
    return 0;
}