from launch import LaunchDescription
from launch_ros.actions import Node
import os
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    pkg = get_package_share_directory('module_manager_hub')
    config = os.path.join(pkg, 'config', 'modules.yaml')

    return LaunchDescription([
        Node(
            package='module_manager_hub',
            executable='module_manager_node',
            name='module_manager_hub',
            parameters=[{
                'config_path': config
            }],
            output='screen'
        )
    ])