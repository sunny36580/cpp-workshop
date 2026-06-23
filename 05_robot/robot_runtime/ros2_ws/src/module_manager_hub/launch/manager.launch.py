from launch import LaunchDescription
from launch_ros.actions import Node
import os
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    pkg = get_package_share_directory('module_manager_hub')
    config_path = os.path.join(pkg, 'config', 'modules.yaml')

    return LaunchDescription([
        # 主进程（CameraStreamer + HeartbeatCollector，同一进程内）
        # ModuleManager/SerialJoyBridge 阶段二启用
        Node(
            package='module_manager_hub',
            executable='module_manager_node',
            name='module_manager_hub',
            parameters=[{'config_path': config_path}],
            output='screen'
        ),
    ])