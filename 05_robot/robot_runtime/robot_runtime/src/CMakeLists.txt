# ============================================================================
# 分层编译 — src/ 子目录
# 依赖方向（禁止反向依赖）由顶层 CMakeLists.txt 统一管理
#
#   common (INTERFACE, header-only)
#      ↑
#   runtime_lib  ←  orchestration_lib
#      ↑                ↑
#   capability_lib (INTERFACE, header-only)
#                       ↑
#                   gateway_lib
#                       ↑
#                   robot (executable)
# ============================================================================

# --------------------------------------------------------------------------
# Layer 0 — common_lib: 公共基础库 (header-only)
# --------------------------------------------------------------------------
add_library(common_lib INTERFACE)
target_include_directories(common_lib INTERFACE .)
target_link_libraries(common_lib INTERFACE yaml-cpp)

# --------------------------------------------------------------------------
# Layer 1 — runtime_lib: 运行时基础设施层
#   依赖: common_lib
# --------------------------------------------------------------------------
add_library(runtime_lib STATIC
    runtime/process/service_manager/service_manager.cpp
    runtime/process/dependency_manager/dependency_manager.cpp
    runtime/monitor/monitor_manager.cpp
    runtime/monitor/heartbeat/heartbeat_monitor.cpp
    runtime/service_access/service_access_manager.cpp
)
target_include_directories(runtime_lib PUBLIC .)
target_link_libraries(runtime_lib PUBLIC common_lib pthread)

# --------------------------------------------------------------------------
# Layer 2 — orchestration_lib: 业务编排层
#   依赖: runtime_lib
# --------------------------------------------------------------------------
add_library(orchestration_lib STATIC
    orchestration/mode/mode_manager.cpp
)
target_include_directories(orchestration_lib PUBLIC .)
target_link_libraries(orchestration_lib PUBLIC runtime_lib)

# --------------------------------------------------------------------------
# Layer 3 — capability_lib: 能力抽象层 (header-only, 仅依赖 common)
#   依赖: common_lib
# --------------------------------------------------------------------------
add_library(capability_lib INTERFACE)
target_include_directories(capability_lib INTERFACE .)
target_link_libraries(capability_lib INTERFACE common_lib)

# --------------------------------------------------------------------------
# Layer 4 — gateway_lib: 外部接入网关层
#   依赖: orchestration_lib + capability_lib
# --------------------------------------------------------------------------
add_library(gateway_lib STATIC
    gateway/tcp/tcp_server.cpp
)
target_include_directories(gateway_lib PUBLIC .)
target_link_libraries(gateway_lib PUBLIC orchestration_lib capability_lib)

# --------------------------------------------------------------------------
# Layer 5 — adapter_lib: 外部框架适配层（可选 ROS2）
#   依赖: runtime_lib
# --------------------------------------------------------------------------
add_library(adapter_lib STATIC
    adapter/ros2/ros2_heartbeat_source.cpp
)
target_include_directories(adapter_lib PUBLIC .)
target_link_libraries(adapter_lib PUBLIC runtime_lib)

# 如果找到 ROS2，链接 rclcpp
find_package(rclcpp QUIET)
if(rclcpp_FOUND)
    target_include_directories(adapter_lib SYSTEM PUBLIC ${rclcpp_INCLUDE_DIRS})
    target_link_libraries(adapter_lib PUBLIC ${rclcpp_LIBRARIES})
    target_compile_definitions(adapter_lib PUBLIC HAS_ROS2=1)
    message(STATUS "ROS2 found: adapter_lib will include ROS2 heartbeat source")
else()
    message(STATUS "ROS2 not found: adapter_lib built without ROS2 support")
endif()

# --------------------------------------------------------------------------
# 主程序 — 组合所有层
# --------------------------------------------------------------------------
add_executable(robot
    core/runtime.cpp
    gateway/cli/main.cpp
)
target_include_directories(robot PRIVATE .)
target_link_libraries(robot PRIVATE gateway_lib adapter_lib)

# ============================================================================
# Install — 库/可执行文件 + 服务目录（config/tools/脚本由顶层 CMakeLists 安装）
# ============================================================================

install(TARGETS robot runtime_lib orchestration_lib gateway_lib adapter_lib
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# ============================================================================
# 单元测试（由 BUILD_TESTING 控制）
# ============================================================================
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
