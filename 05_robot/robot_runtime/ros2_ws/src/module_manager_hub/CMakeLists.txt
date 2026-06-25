set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE INTERNAL "")

cmake_minimum_required(VERSION 3.8)
project(module_manager_hub)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(POLICY CMP0148)
  cmake_policy(SET CMP0148 OLD)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# OpenCV / FFmpeg 依赖（CameraStreamerCore 使用）
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)
find_package(cv_bridge REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavutil libswscale)

# 生成消息
rosidl_generate_interfaces(${PROJECT_NAME}
  msg/Robotarmcontrol.msg
  msg/Robotarmstatus.msg
  DEPENDENCIES std_msgs
)

include_directories(include)

# =====================================================================
# 编译静态库：纯业务逻辑（无 ROS 依赖）
# =====================================================================
add_library(module_manager_core STATIC
  src/common/serial_reader.cpp
  src/core/joy_bridge_core.cpp
  src/core/camera_streamer_core.cpp
  src/core/heartbeat_collector_core.cpp
  src/core/module_manager_core.cpp
)

target_include_directories(module_manager_core PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(module_manager_core
  yaml-cpp
  ${Boost_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${FFMPEG_LIBRARIES}
)

# =====================================================================
# 编译可执行文件：ROS Node 层（链接 core 库）
# =====================================================================
add_executable(module_manager_node
  src/main.cpp
  src/ros/serial_joy_bridge_node.cpp
  src/ros/camera_streamer_node.cpp
  src/ros/heartbeat_collector_node.cpp
  src/ros/module_manager_node.cpp
)

ament_target_dependencies(module_manager_node
  rclcpp
  std_msgs
  sensor_msgs
  cv_bridge
)

target_link_libraries(module_manager_node
  module_manager_core
)

# 让 rosidl 生成的消息头文件可以被 module_manager_node 找到
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(module_manager_node "${cpp_typesupport_target}")

# =====================================================================
# 纯 C++ 单测可执行文件（无 ROS 依赖，仅链接 core 库）
# =====================================================================
add_executable(test_joy_bridge_core
  test/test_joy_bridge_core.cpp
)
target_link_libraries(test_joy_bridge_core module_manager_core)

add_executable(test_camera_streamer_core
  test/test_camera_streamer_core.cpp
)
target_link_libraries(test_camera_streamer_core module_manager_core)

add_executable(test_heartbeat_core
  test/test_heartbeat_core.cpp
)
target_link_libraries(test_heartbeat_core module_manager_core)

add_executable(test_module_manager_core
  test/test_module_manager_core.cpp
)
target_link_libraries(test_module_manager_core module_manager_core)

# 安装
install(TARGETS
  module_manager_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

# 启动/停止脚本（安装到 workspace 根目录 install/）
install(CODE "file(INSTALL \"${CMAKE_CURRENT_SOURCE_DIR}/start.sh\" \"${CMAKE_CURRENT_SOURCE_DIR}/stop.sh\"
       DESTINATION \"${CMAKE_INSTALL_PREFIX}/..\"
       TYPE PROGRAM)")


ament_package()