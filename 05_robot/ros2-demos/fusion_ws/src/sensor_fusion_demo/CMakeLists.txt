cmake_minimum_required(VERSION 3.8)
project(sensor_fusion_demo)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 1. 找依赖
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(message_filters REQUIRED)
find_package(builtin_interfaces REQUIRED)
find_package(rosidl_default_generators REQUIRED)

# 2. 编译自定义消息
rosidl_generate_interfaces(${PROJECT_NAME}
  msg/ImageMsg.msg
  msg/LidarMsg.msg
  msg/SyncedBundleMsg.msg
  msg/WorldModelMsg.msg
  DEPENDENCIES builtin_interfaces
)

# 3. 包含头文件路径
include_directories(include)

# 4. 编译各个节点
# 相机节点
add_executable(camera_node src/camera_node.cpp)
ament_target_dependencies(camera_node rclcpp sensor_msgs)
rosidl_get_typesupport_target(cpp_typesupport_target ${PROJECT_NAME} rosidl_typesupport_cpp)
target_link_libraries(camera_node ${cpp_typesupport_target})

# 雷达节点
add_executable(lidar_node src/lidar_node.cpp)
ament_target_dependencies(lidar_node rclcpp sensor_msgs)
target_link_libraries(lidar_node ${cpp_typesupport_target})

# 时间同步节点
add_executable(sync_node src/sync_node.cpp)
ament_target_dependencies(sync_node rclcpp message_filters)
target_link_libraries(sync_node ${cpp_typesupport_target})

# 融合节点
add_executable(fusion_node src/fusion_node.cpp)
ament_target_dependencies(fusion_node rclcpp)
target_link_libraries(fusion_node ${cpp_typesupport_target})

# 世界模型节点
add_executable(world_model_node src/world_model_node.cpp)
ament_target_dependencies(world_model_node rclcpp)
target_link_libraries(world_model_node ${cpp_typesupport_target})

# 5. 安装可执行文件
install(TARGETS
  camera_node
  lidar_node
  sync_node
  fusion_node
  world_model_node
  DESTINATION lib/${PROJECT_NAME}
)

# 6. 安装launch文件
install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

ament_package()