cmake_minimum_required(VERSION 3.16)
project(robot VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# 机器人工作区顶层 CMakeLists.txt
#
# 统一编译入口，支持：
#   - runtime/      : 运行时框架（独立于 ROS2）
#   - services/     : 业务服务（ROS2 Launch / 独立进程）
#   - plugins/      : 动态库插件（Phase6 启用）
# ============================================================================
#
# 用法:
#   编译 runtime:  cmake -S runtime -B build/runtime && cmake --build build/runtime
#   编译 ROS2:    colcon build --packages-select module_manager_hub
#   全部编译:     参考 start_runtime.sh
# ============================================================================

# runtime 框架（独立编译，不依赖 ROS2）
# 注: runtime 有独立 CMakeLists.txt，可直接编译
#     此处不 add_subdirectory，避免强制耦合
message(STATUS "=== Robot Workspace ===")
message(STATUS "Runtime: cmake -S runtime -B build/runtime && cmake --build build/runtime")
message(STATUS "ROS2:    colcon build --packages-select <pkg>")
