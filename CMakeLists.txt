cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

find_package(Threads REQUIRED)
project(hftp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Main static library
add_library(hftp STATIC
  src/network/socket.cpp
  src/protocol/reply.cpp
  src/protocol/command_dispatcher.cpp
  src/filesystem/ascii_transfer.cpp
  src/session/session.cpp
  src/filesystem/file_repository.cpp
  src/filesystem/listing_formatter.cpp
  src/integrity/sha256.cpp
  src/logging/logger.cpp
  src/session/session_service.cpp
  src/ui/progress.cpp
    src/network/socket.cpp
    src/protocol/reply.cpp
    src/rdt/crc32.cpp
    src/rdt/packet.cpp
    src/rdt/reliable_transport.cpp
    src/transfer/file_transfer.cpp
)

# Link Windows Socket Library (WinSock)
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
    target_link_libraries(hftp PRIVATE ws2_32)
endif()

# Applications
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/client_main.cpp")
    add_executable(hftp_client apps/client_main.cpp)
    target_link_libraries(hftp_client PRIVATE hftp)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/server_main.cpp")
    add_executable(hftp_server apps/server_main.cpp)
    target_link_libraries(hftp_server PRIVATE hftp)
endif()

# Testing Configuration
option(BUILD_TESTING "Build unit tests" ON)

if(BUILD_TESTING)
    enable_testing()

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
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/socket_tests.cpp")
        add_executable(socket_tests tests/socket_tests.cpp)
        target_link_libraries(socket_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(socket_tests PRIVATE ws2_32)
        endif()
        add_test(NAME socket_tests COMMAND socket_tests)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/reply_tests.cpp")
        add_executable(reply_tests tests/reply_tests.cpp)
        target_link_libraries(reply_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(reply_tests PRIVATE ws2_32)
        endif()
        add_test(NAME reply_tests COMMAND reply_tests)
    endif()
  add_executable(session_service_tests tests/session_test_1.cpp)
  target_link_libraries(session_service_tests PRIVATE hftp)
  add_test(NAME session_management COMMAND session_service_tests)
endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/packet_tests.cpp")
        add_executable(packet_tests tests/packet_tests.cpp)
        target_link_libraries(packet_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(packet_tests PRIVATE ws2_32)
        endif()
        add_test(NAME packet_wire COMMAND packet_tests)
    endif()

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/rdt_tests.cpp")
        add_executable(rdt_tests tests/rdt_tests.cpp)
        target_link_libraries(rdt_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(rdt_tests PRIVATE ws2_32)
        endif()
        add_test(NAME rdt_loopback COMMAND rdt_tests)
    endif()

    # --- Transfer Tests ---
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/transfer_tests.cpp")
        add_executable(transfer_tests tests/transfer_tests.cpp)
        target_link_libraries(transfer_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(transfer_tests PRIVATE ws2_32)
        endif()
        add_test(NAME transfer_tests COMMAND transfer_tests)
    endif()

    # --- Cancellation Tests ---
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/cancellation_tests.cpp")
        add_executable(cancellation_tests tests/cancellation_tests.cpp)
        target_link_libraries(cancellation_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(cancellation_tests PRIVATE ws2_32)
        endif()
        add_test(NAME cancellation_tests COMMAND cancellation_tests)
    endif()

    # --- Sliding Window Tests (Go-Back-N) ---
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/sliding_window_tests.cpp")
        add_executable(sliding_window_tests tests/sliding_window_tests.cpp)
        target_link_libraries(sliding_window_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(sliding_window_tests PRIVATE ws2_32)
        endif()
        add_test(NAME sliding_window_tests COMMAND sliding_window_tests)
    endif()

    if(MINGW)
        if(TARGET socket_tests)
            target_link_options(socket_tests PRIVATE -static)
        endif()
        if(TARGET reply_tests)
            target_link_options(reply_tests PRIVATE -static)
        endif()
        if(TARGET packet_tests)
            target_link_options(packet_tests PRIVATE -static)
        endif()
        if(TARGET rdt_tests)
            target_link_options(rdt_tests PRIVATE -static)
        endif()
        if(TARGET transfer_tests)
            target_link_options(transfer_tests PRIVATE -static)
        endif()
        if(TARGET cancellation_tests)
            target_link_options(cancellation_tests PRIVATE -static)
        endif()
        if(TARGET sliding_window_tests)
            target_link_options(sliding_window_tests PRIVATE -static)
        endif()
    endif()
endif()
target_link_libraries(hftp_client PRIVATE hftp)
