cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hftp STATIC
  src/network/socket.cpp
  src/protocol/reply.cpp
  src/control/crlf_framer.cpp
  src/control/command_parser.cpp
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

  add_executable(crlf_framer_tests tests/crlf_framer_tests.cpp)
  target_link_libraries(crlf_framer_tests PRIVATE hftp)
  add_test(NAME crlf_framer COMMAND crlf_framer_tests)

  add_executable(command_parser_tests tests/command_parser_tests.cpp)
  target_link_libraries(command_parser_tests PRIVATE hftp)
  add_test(NAME command_parser COMMAND command_parser_tests)
endif()

add_executable(hftp_server apps/server_main.cpp)
target_link_libraries(hftp_server PRIVATE hftp)

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp)
