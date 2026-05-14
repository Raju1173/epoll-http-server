cmake_minimum_required(VERSION 3.16)
project(epoll_http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(projectConfigs INTERFACE)

target_compile_options(projectConfigs INTERFACE -Wall -Wextra -Wpedantic -stdlib=libc++)

target_link_options(projectConfigs INTERFACE -stdlib=libc++)

target_include_directories(projectConfigs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/headers)

add_executable(server src/Server.cpp)
target_link_libraries(server PRIVATE projectConfigs)

add_executable(bench benchmarks/ServerBench.cpp)
target_link_libraries(bench PRIVATE projectConfigs)

add_executable(test tests/ServerTest.cpp)
target_link_libraries(test PRIVATE projectConfigs)
