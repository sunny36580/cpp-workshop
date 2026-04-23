cmake_minimum_required(VERSION 3.10)
project(sensor_fusion_demo)

# 核心修复：改为 C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 头文件路径
include_directories(
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/utils
    ${PROJECT_SOURCE_DIR}/core
    ${PROJECT_SOURCE_DIR}/sensor
)

# 生成可执行文件
add_executable(fusion_demo main.cpp)

# 链接线程库
find_package(Threads REQUIRED)
target_link_libraries(fusion_demo PRIVATE Threads::Threads)