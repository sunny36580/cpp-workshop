from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
import os
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    pkg = get_package_share_directory('module_manager_hub')
    config = os.path.join(pkg, 'config', 'modules.yaml')

    # 串口设备参数，默认 /dev/ttyUSB0
    serial_port_arg = DeclareLaunchArgument(
        'joy_serial_port',
        default_value='/dev/ttyUSB0',
        description='Serial port for joy bridge'
    )

    return LaunchDescription([
        serial_port_arg,

        # 模块管理器
        Node(
            package='module_manager_hub',
            executable='module_manager_node',
            name='module_manager_hub',
            parameters=[{
                'config_path': config
            }],
            output='screen'
        ),

        # 串口 Joy Bridge
        Node(
            package='module_manager_hub',
            executable='serial_joy_bridge_node',
            name='serial_joy_bridge',
            parameters=[{
                'serial_port': LaunchConfiguration('joy_serial_port'),
                'serial_baud': 115200
            }],
            output='screen'
        ),
    ])