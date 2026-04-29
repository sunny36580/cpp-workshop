cmake_minimum_required(VERSION 3.10)
project(cpu_cache)

set(CMAKE_CXX_STANDARD 17)

# 1. 先找到线程库
find_package(Threads REQUIRED)

# 2. 添加可执行文件目标
add_executable(cpu_cache main.cpp)

# 3. 链接线程库
target_link_libraries(cpu_cache Threads::Threads)