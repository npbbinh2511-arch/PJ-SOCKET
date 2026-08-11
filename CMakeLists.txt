cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_library(hftp STATIC
    src/client/client.cpp
    src/client/cli.cpp
    src/client/data_channel.cpp
    src/control/authenticator.cpp
    src/control/command_dispatcher.cpp
    src/control/command_parser.cpp
    src/control/crlf_framer.cpp
    src/filesystem/ascii_transfer.cpp
    src/filesystem/file_repository.cpp
    src/filesystem/listing_formatter.cpp
    src/integrity/sha256.cpp
    src/logging/logger.cpp
    src/network/socket.cpp
    src/protocol/reply.cpp
    src/rdt/crc32.cpp
    src/rdt/packet.cpp
    src/rdt/reliable_transport.cpp
    src/server/server.cpp
    src/session/session.cpp
    src/session/session_service.cpp
    src/transfer/data_connection_manager.cpp
    src/transfer/file_transfer.cpp
    src/ui/progress.cpp
)

target_include_directories(hftp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hftp PUBLIC Threads::Threads)

if(WIN32)
    target_link_libraries(hftp PUBLIC ws2_32)
endif()

add_executable(hftp_server apps/server_main.cpp)
target_link_libraries(hftp_server PRIVATE hftp)

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp)

option(BUILD_TESTING "Build unit tests" ON)

if(BUILD_TESTING)
    enable_testing()

    function(add_hftp_test target source test_name)
        add_executable(${target} ${source})
        target_link_libraries(${target} PRIVATE hftp)
        add_test(NAME ${test_name} COMMAND ${target})
    endfunction()

    add_hftp_test(ascii_transfer_tests tests/ascii_transfer_tests.cpp ascii_transfer)
    add_hftp_test(authenticator_tests tests/authenticator_tests.cpp authentication)
    add_hftp_test(cancellation_tests tests/cancellation_tests.cpp cancellation)
    add_hftp_test(client_tests tests/client_tests.cpp tcp_client)
    add_hftp_test(client_data_channel_tests tests/client_data_channel_tests.cpp client_data_channel)
    add_hftp_test(command_dispatcher_tests tests/command_dispatcher_tests.cpp command_dispatcher)
    add_hftp_test(command_parser_tests tests/command_parser_tests.cpp command_parser)
    add_hftp_test(crlf_framer_tests tests/crlf_framer_tests.cpp crlf_framer)
    add_hftp_test(data_connection_manager_tests tests/data_connection_manager_tests.cpp data_connection_manager)
    add_hftp_test(filesystem_tests tests/filesystem_test.cpp filesystem_repository)
    add_hftp_test(hybrid_end_to_end_tests tests/hybrid_end_to_end_tests.cpp hybrid_end_to_end)
    add_hftp_test(listing_formatter_tests tests/listing_formatter_tests.cpp listing_formatters)
    add_hftp_test(member_c_integration_tests tests/member_c_integration_tests.cpp member_c_integration)
    add_hftp_test(member_c_advanced_tests tests/member_c_advanced_tests.cpp member_c_advanced)
    add_hftp_test(member_c_transfer_tests tests/member_c_transfer_tests.cpp member_c_transfer)
    add_hftp_test(packet_tests tests/packet_tests.cpp packet_wire)
    add_hftp_test(progress_logging_tests tests/progress_logging_tests.cpp progress_and_logging)
    add_hftp_test(rdt_tests tests/rdt_tests.cpp rdt_loopback)
    add_hftp_test(reply_tests tests/reply_tests.cpp reply_formatter)
    add_hftp_test(server_tests tests/server_tests.cpp tcp_server)
    add_hftp_test(session_tests tests/session_tests.cpp session_state)
    add_hftp_test(session_service_tests tests/session_test_1.cpp unified_session_commands)
    add_hftp_test(sha256_tests tests/sha256_tests.cpp sha256_integrity)
    add_hftp_test(sliding_window_tests tests/sliding_window_tests.cpp sliding_window)
    add_hftp_test(socket_tests tests/socket_tests.cpp socket_raii)
    add_hftp_test(transfer_tests tests/transfer_tests.cpp file_transfer)
endif()
