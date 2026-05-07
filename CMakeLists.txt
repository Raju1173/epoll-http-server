cmake_minimum_required(VERSION 3.16)

project(epoll_http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server src/Server.cpp)

target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
