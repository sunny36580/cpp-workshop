cmake_minimum_required(VERSION 3.10)
project(zero_copy)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-O2 -Wall)

add_executable(zc_server server.cpp)
add_executable(zc_client client.cpp)