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

# 生成消息（模块管理器不再管理进程生命周期，移除 ModuleStatus / ModuleControl）
rosidl_generate_interfaces(${PROJECT_NAME}
  msg/Robotarmcontrol.msg
  msg/Robotarmstatus.msg
  DEPENDENCIES std_msgs
)

include_directories(include)

# 编译可执行文件（SerialJoyBridge + CameraStreamer + HeartbeatCollector）
# ModuleManager 暂不编译（串口遥控协议阶段二启用，源文件保留）
add_executable(module_manager_node
  src/main.cpp
  src/serial_joy_bridge.cpp
  src/serial_reader.cpp
  src/usb_camera_sender_node.cpp
  src/heartbeat_collector.cpp
)

# OpenCV / FFmpeg 依赖（CameraStreamer 使用）
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)
find_package(cv_bridge REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavutil libswscale)

ament_target_dependencies(module_manager_node
  rclcpp
  std_msgs
  sensor_msgs
  cv_bridge
)

target_include_directories(module_manager_node PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(module_manager_node
  yaml-cpp
  ${Boost_LIBRARIES}
  ${OpenCV_LIBRARIES}
  ${FFMPEG_LIBRARIES}
)

# 安装到bin目录（三个 node 全部合并至 module_manager_node 同一进程）
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