cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Chuyển hftp thành Static Library và thêm source file triển khai
add_library(hftp STATIC
    src/filesystem/file_repository.cpp
)

target_include_directories(hftp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    target_link_libraries(hftp PUBLIC ws2_32)
endif()

option(HFTP_BUILD_TEST_SKELETONS "Expose non-runnable test skeleton files" OFF)

add_executable(hftp_server apps/server_main.cpp)
target_link_libraries(hftp_server PRIVATE hftp)

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp) 

add_executable(filesystem_test tests/filesystem_test.cpp)
target_link_libraries(filesystem_test PRIVATE hftp)