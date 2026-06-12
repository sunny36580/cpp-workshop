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
find_package(yaml-cpp REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(Boost REQUIRED COMPONENTS system)

# 生成消息 + 服务
rosidl_generate_interfaces(${PROJECT_NAME}
  msg/ModuleStatus.msg
  msg/Robotarmcontrol.msg
  msg/Robotarmstatus.msg
  srv/ModuleControl.srv
  DEPENDENCIES std_msgs
)

include_directories(include)

# 编译可执行文件
add_executable(module_manager_node
  src/main.cpp
  src/module_manager.cpp
)

# 链接 yaml-cpp
ament_target_dependencies(module_manager_node
  rclcpp
  std_msgs
  geometry_msgs
)

target_link_libraries(module_manager_node
  yaml-cpp
  ${Boost_LIBRARIES}
)


# 链接自定义消息（两个可执行文件都需要）
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(module_manager_node ${cpp_typesupport_target})

# 编译测试机械臂控制器节点
add_executable(test_arm_controller_node
  src/test_arm_controller_node.cpp
)

ament_target_dependencies(test_arm_controller_node
  rclcpp
  std_msgs
)

# 链接自定义消息
rosidl_get_typesupport_target(test_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(test_arm_controller_node ${test_typesupport_target})

# ========== 相机推流节点 ==========
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs highgui)

find_package(sensor_msgs REQUIRED)
find_package(cv_bridge REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavutil libswscale)

add_executable(camera_streamer_node
  src/usb_camera_sender_node.cpp
)

ament_target_dependencies(camera_streamer_node
  rclcpp
  std_msgs
  sensor_msgs
  cv_bridge
)

target_include_directories(camera_streamer_node PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(camera_streamer_node
  ${OpenCV_LIBRARIES}
  ${FFMPEG_LIBRARIES}
)

# ========== 串口 Joy Bridge 节点 ==========
add_executable(serial_joy_bridge_node
  src/serial_joy_bridge.cpp
  src/serial_joy_bridge_main.cpp
)

ament_target_dependencies(serial_joy_bridge_node
  rclcpp
  std_msgs
  sensor_msgs
)

target_link_libraries(serial_joy_bridge_node
  yaml-cpp
  ${Boost_LIBRARIES}
)

# 安装到bin目录
install(TARGETS
  module_manager_node
  test_arm_controller_node
  camera_streamer_node
  serial_joy_bridge_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()