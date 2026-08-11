cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hftp STATIC
  src/network/socket.cpp
  src/protocol/reply.cpp
  src/session/session.cpp
  src/filesystem/file_repository.cpp
  src/control/crlf_framer.cpp
  src/control/command_parser.cpp
  src/control/authenticator.cpp
  src/client/client.cpp
  src/server/server.cpp
  src/transfer/data_connection_manager.cpp
  src/control/command_dispatcher.cpp
)
target_include_directories(hftp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hftp PUBLIC Threads::Threads)
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

  add_executable(session_tests tests/session_tests.cpp)
  target_link_libraries(session_tests PRIVATE hftp)
  add_test(NAME session_state COMMAND session_tests)

  add_executable(crlf_framer_tests tests/crlf_framer_tests.cpp)
  target_link_libraries(crlf_framer_tests PRIVATE hftp)
  add_test(NAME crlf_framer COMMAND crlf_framer_tests)

  add_executable(command_parser_tests tests/command_parser_tests.cpp)
  target_link_libraries(command_parser_tests PRIVATE hftp)
  add_test(NAME command_parser COMMAND command_parser_tests)

  add_executable(authenticator_tests tests/authenticator_tests.cpp)
  target_link_libraries(authenticator_tests PRIVATE hftp)
  add_test(NAME authentication COMMAND authenticator_tests)

  add_executable(client_tests tests/client_tests.cpp)
  target_link_libraries(client_tests PRIVATE hftp)
  add_test(NAME tcp_client COMMAND client_tests)

  add_executable(server_tests tests/server_tests.cpp)
  target_link_libraries(server_tests PRIVATE hftp)
  add_test(NAME tcp_server COMMAND server_tests)

  add_executable(data_connection_manager_tests tests/data_connection_manager_tests.cpp)
  target_link_libraries(data_connection_manager_tests PRIVATE hftp)
  add_test(NAME data_connection_manager COMMAND data_connection_manager_tests)

  add_executable(command_dispatcher_tests tests/command_dispatcher_tests.cpp)
  target_link_libraries(command_dispatcher_tests PRIVATE hftp)
  add_test(NAME command_dispatcher COMMAND command_dispatcher_tests)

  add_executable(filesystem_test tests/filesystem_test.cpp)
  target_link_libraries(filesystem_test PRIVATE hftp)
  add_test(NAME filesystem_repository COMMAND filesystem_test)

  add_executable(session_service_tests tests/session_test_1.cpp)
  target_link_libraries(session_service_tests PRIVATE hftp)
  add_test(NAME session_management COMMAND session_service_tests)
endif()

add_executable(hftp_server apps/server_main.cpp)
target_link_libraries(hftp_server PRIVATE hftp)

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp)
