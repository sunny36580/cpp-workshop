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
)

target_link_libraries(module_manager_node
  yaml-cpp
  ${Boost_LIBRARIES}
)

# 链接自定义消息
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} "rosidl_typesupport_cpp")
target_link_libraries(module_manager_node ${cpp_typesupport_target})

# 安装
install(TARGETS
  module_manager_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY launch config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()