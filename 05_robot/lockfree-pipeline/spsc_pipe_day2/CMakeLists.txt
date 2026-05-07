cmake_minimum_required(VERSION 3.10)
project(lightning_pipe_day1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")  # 必须加线程库

add_executable(pipe main.cpp)