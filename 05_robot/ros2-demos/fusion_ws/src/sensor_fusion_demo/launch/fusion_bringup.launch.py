from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(package="sensor_fusion_demo", executable="camera_node", output="screen"),
        Node(package="sensor_fusion_demo", executable="lidar_node", output="screen"),
        Node(package="sensor_fusion_demo", executable="sync_node", output="screen"),
        Node(package="sensor_fusion_demo", executable="fusion_node", output="screen"),
        Node(package="sensor_fusion_demo", executable="world_model_node", output="screen"),
    ])