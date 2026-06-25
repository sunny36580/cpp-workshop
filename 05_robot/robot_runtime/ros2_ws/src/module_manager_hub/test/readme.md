# 编译
colcon build --packages-select module_manager_hub

# 运行单测（无需 ROS runtime）
./build/module_manager_hub/test_joy_bridge_core
./build/module_manager_hub/test_heartbeat_core
./build/module_manager_hub/test_module_manager_core
./build/module_manager_hub/test_camera_streamer_core