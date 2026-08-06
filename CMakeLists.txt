cmake_minimum_required(VERSION 3.20)
project(hybrid_ftp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(hftp INTERFACE)
target_include_directories(hftp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
if(WIN32)
  target_link_libraries(hftp INTERFACE ws2_32)
endif()

option(HFTP_BUILD_TEST_SKELETONS "Expose non-runnable test skeleton files" OFF)

add_executable(hftp_server apps/server_main.cpp)
target_link_libraries(hftp_server PRIVATE hftp)

add_executable(hftp_client apps/client_main.cpp)
target_link_libraries(hftp_client PRIVATE hftp)
