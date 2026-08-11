cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hftp STATIC
  src/network/socket.cpp
  src/protocol/reply.cpp
  src/protocol/command_dispatcher.cpp
  src/filesystem/ascii_transfer.cpp
  src/filesystem/file_repository.cpp
  src/filesystem/listing_formatter.cpp
  src/integrity/sha256.cpp
  src/logging/logger.cpp
  src/session/session_service.cpp
  src/ui/progress.cpp
)
target_include_directories(hftp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
  target_link_libraries(hftp PUBLIC ws2_32)
endif()

option(HFTP_BUILD_TEST_SKELETONS "Expose non-runnable test skeleton files" OFF)

include(CTest)
if(BUILD_TESTING)
  add_executable(socket_tests tests/socket_tests.cpp)
  target_link_libraries(socket_tests PRIVATE hftp)
  add_test(NAME socket_ownership COMMAND socket_tests)

  add_executable(reply_tests tests/reply_tests.cpp)
  target_link_libraries(reply_tests PRIVATE hftp)
  add_test(NAME reply_formatter COMMAND reply_tests)

  add_executable(filesystem_test tests/filesystem_test.cpp)
  target_link_libraries(filesystem_test PRIVATE hftp)
  add_test(NAME filesystem_repository COMMAND filesystem_test)

  add_executable(session_test tests/session_test_1.cpp)
  target_link_libraries(session_test PRIVATE hftp)
  add_test(NAME session_management COMMAND session_test)

  add_executable(sha256_tests tests/sha256_tests.cpp)
  target_link_libraries(sha256_tests PRIVATE hftp)
  add_test(NAME sha256_integrity COMMAND sha256_tests)

  add_executable(ascii_transfer_tests tests/ascii_transfer_tests.cpp)
  target_link_libraries(ascii_transfer_tests PRIVATE hftp)
  add_test(NAME ascii_transfer COMMAND ascii_transfer_tests)

  add_executable(progress_logging_tests tests/progress_logging_tests.cpp)
  target_link_libraries(progress_logging_tests PRIVATE hftp)
  add_test(NAME progress_and_logging COMMAND progress_logging_tests)

  add_executable(member_c_integration_tests tests/member_c_integration_tests.cpp)
  target_link_libraries(member_c_integration_tests PRIVATE hftp)
  add_test(NAME member_c_integration COMMAND member_c_integration_tests)

  add_executable(listing_formatter_tests tests/listing_formatter_tests.cpp)
  target_link_libraries(listing_formatter_tests PRIVATE hftp)
  add_test(NAME listing_formatters COMMAND listing_formatter_tests)
endif()

add_executable(hftp_server apps/server_main.cpp)
target_link_libraries(hftp_server PRIVATE hftp)

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp)
