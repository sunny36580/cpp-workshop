cmake_minimum_required(VERSION 3.10)
project(tcp_tuning)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-O2 -Wall)

add_executable(tcp_server server.cpp)
add_executable(tcp_client client.cpp)

target_link_libraries(tcp_server pthread)
target_link_libraries(tcp_client pthread)