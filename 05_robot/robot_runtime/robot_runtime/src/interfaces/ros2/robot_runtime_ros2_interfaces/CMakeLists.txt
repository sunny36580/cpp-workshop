cmake_minimum_required(VERSION 3.16)
project(robot_runtime_ros2_interfaces)

# 仅当 ROS2 可用时构建
find_package(rosidl_default_generators REQUIRED)
find_package(builtin_interfaces REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
    msg/Heartbeat.msg
    msg/ServiceState.msg
    srv/LifecycleCommand.srv
    srv/GetServiceState.srv
    DEPENDENCIES builtin_interfaces
)
