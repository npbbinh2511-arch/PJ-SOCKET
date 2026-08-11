cmake_minimum_required(VERSION 3.20)
project(hftp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Main static library
add_library(hftp STATIC
    src/network/socket.cpp
    src/protocol/reply.cpp
    src/rdt/crc32.cpp
    src/rdt/packet.cpp
    src/rdt/reliable_transport.cpp
    src/transfer/file_transfer.cpp
)

# Link Windows Socket Library (WinSock)
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

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/packet_tests.cpp")
        add_executable(packet_tests tests/packet_tests.cpp)
        target_link_libraries(packet_tests PRIVATE hftp)
        if(WIN32)
            target_link_libraries(packet_tests PRIVATE ws2_32)
        endif()
        add_test(NAME packet_wire COMMAND packet_tests)
    endif()

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