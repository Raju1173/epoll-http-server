cmake_minimum_required(VERSION 3.16)

project(epoll_http_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(headers)

add_executable(server src/Server.cpp headers/Socket.h headers/Server.h)

target_compile_options(server PRIVATE -Wall -Wextra -Wpedantic)
target_compile_options(server PRIVATE -stdlib=libc++)
target_link_options(server PRIVATE -stdlib=libc++)
