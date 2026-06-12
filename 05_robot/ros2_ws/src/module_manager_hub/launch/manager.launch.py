from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
import os
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    pkg = get_package_share_directory('module_manager_hub')
    modules_config = os.path.join(pkg, 'config', 'modules.yaml')
    joy_config     = os.path.join(pkg, 'config', 'serial_joy_bridge.yaml')

    return LaunchDescription([
        # 模块管理器（读 modules.yaml）
        Node(
            package='module_manager_hub',
            executable='module_manager_node',
            name='module_manager_hub',
            parameters=[{'config_path': modules_config}],
            output='screen'
        ),

        # 串口摇杆桥接（读 serial_joy_bridge.yaml）
        Node(
            package='module_manager_hub',
            executable='serial_joy_bridge_node',
            name='serial_joy_bridge',
            parameters=[{'config_path': joy_config}],
            output='screen'
        ),
    ])