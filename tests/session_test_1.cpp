#include <filesystem>
#include <fstream>
#include <iostream>

#include "hftp/filesystem/file_repository.h"
#include "hftp/protocol/command_dispatcher.h"
#include "hftp/session/session.h"
#include "hftp/session/session_service.h"

namespace fs = std::filesystem;
using namespace hftp::filesystem;
using namespace hftp::session;
using namespace hftp::common;
using namespace hftp::protocol;

#define TEST_CHECK(cond)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  [FAIL] " << __FILE__ << ":" << __LINE__                  \
                << " -> Condition failed: " #cond << "\n";                     \
      return false;                                                            \
    }                                                                          \
  } while (0)

// 1. Test CWD & Sandbox
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

  TEST_CHECK(service.get_pwd(session) == "/");
  TEST_CHECK(service.change_directory(session, "docs").error == Error::none);
  TEST_CHECK(service.get_pwd(session) == "/docs");
  TEST_CHECK(service.change_directory(session, "..").error == Error::none);
  TEST_CHECK(service.get_pwd(session) == "/");
  TEST_CHECK(service.change_directory(session, "../../etc").error ==
             Error::permission_denied);
  TEST_CHECK(service.get_pwd(session) == "/");
  TEST_CHECK(service.change_directory(session, "non_existent_folder").error ==
             Error::not_found);

  fs::remove_all(test_root, ec);
  std::cout << "[PASS] Test Session CWD & Sandbox Security successfully!\n";
  return true;
}

// 2. Test PWD, CWD, CDUP Dispatcher Integration
bool test_command_dispatcher_integration() {
  fs::path test_root = fs::temp_directory_path() / "hftp_dispatcher_test";
  std::error_code ec;
  fs::remove_all(test_root, ec);
  fs::create_directories(test_root / "subdir", ec);

  Std_FileRepository repo(test_root);
  SessionService service(repo);
  CommandDispatcher dispatcher(service);
  Session session;

  Command pwd_cmd{"PWD", ""};
  std::string res_pwd = dispatcher.dispatch(session, pwd_cmd);
  TEST_CHECK(res_pwd.rfind("257", 0) == 0);

  Command cwd_cmd{"CWD", "subdir"};
  std::string res_cwd = dispatcher.dispatch(session, cwd_cmd);
  TEST_CHECK(res_cwd.rfind("250", 0) == 0);

  Command cdup_cmd{"CDUP", ""};
  std::string res_cdup = dispatcher.dispatch(session, cdup_cmd);
  TEST_CHECK(res_cdup.rfind("200", 0) == 0 || res_cdup.rfind("250", 0) == 0);

  fs::remove_all(test_root, ec);
  std::cout << "[PASS] Test Command Dispatcher Integration successfully!\n";
  return true;
}

// 3. Test SIZE & MDTM Commands
bool test_size_and_mdtm_commands() {
  fs::path test_root = fs::temp_directory_path() / "hftp_test_size_mdtm";
  std::error_code ec;
  fs::remove_all(test_root, ec);
  fs::create_directories(test_root, ec);

  fs::path sample_file = test_root / "test.txt";
  {
    std::ofstream ofs(sample_file);
    ofs << "Hello World!";
  }

  Std_FileRepository repo(test_root);
  SessionService service(repo);
  CommandDispatcher dispatcher(service);
  Session session;

  Command size_cmd{"SIZE", "test.txt"};
  std::string res_size = dispatcher.dispatch(session, size_cmd);
  TEST_CHECK(res_size.rfind("213 12", 0) == 0);

  Command size_invalid{"SIZE", "not_exist.txt"};
  std::string res_invalid_size = dispatcher.dispatch(session, size_invalid);
  TEST_CHECK(res_invalid_size.rfind("550", 0) == 0);

  Command mdtm_cmd{"MDTM", "test.txt"};
  std::string res_mdtm = dispatcher.dispatch(session, mdtm_cmd);
  TEST_CHECK(res_mdtm.rfind("213 ", 0) == 0);

  fs::remove_all(test_root, ec);
  std::cout << "[PASS] Test SIZE and MDTM commands successfully!\n";
  return true;
}

// 4. Test MKD, RMD, DELE Commands
bool test_mkd_rmd_dele_commands() {
  fs::path test_root = fs::temp_directory_path() / "hftp_test_mkd_rmd_dele";
  std::error_code ec;
  fs::remove_all(test_root, ec);
  fs::create_directories(test_root, ec);

  Std_FileRepository repo(test_root);
  SessionService service(repo);
  CommandDispatcher dispatcher(service);
  Session session;

  Command mkd_cmd{"MKD", "new_folder"};
  std::string res_mkd = dispatcher.dispatch(session, mkd_cmd);
  TEST_CHECK(res_mkd.rfind("257", 0) == 0);
  TEST_CHECK(fs::is_directory(test_root / "new_folder"));

  fs::path temp_file = test_root / "delete_me.txt";
  {
    std::ofstream ofs(temp_file);
    ofs << "bye";
  }
  Command dele_cmd{"DELE", "delete_me.txt"};
  std::string res_dele = dispatcher.dispatch(session, dele_cmd);
  TEST_CHECK(res_dele.rfind("250", 0) == 0);
  TEST_CHECK(!fs::exists(temp_file));

  Command rmd_cmd{"RMD", "new_folder"};
  std::string res_rmd = dispatcher.dispatch(session, rmd_cmd);
  TEST_CHECK(res_rmd.rfind("250", 0) == 0);
  TEST_CHECK(!fs::exists(test_root / "new_folder"));

  fs::remove_all(test_root, ec);
  std::cout << "[PASS] Test MKD, RMD, and DELE commands successfully!\n";
  return true;
}

int main() {
  if (!test_session_cwd_and_sandbox()) return 1;
  if (!test_command_dispatcher_integration()) return 1;
  if (!test_size_and_mdtm_commands()) return 1;
  if (!test_mkd_rmd_dele_commands()) return 1;

  std::cout << "\n==> ALL SESSION & DISPATCHER TESTS PASSED! <==\n";
  return 0;
}