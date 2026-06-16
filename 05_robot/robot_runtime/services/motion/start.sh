#!/bin/bash
# ============================================================================
# Motion Service — 运控服务启动脚本
# ============================================================================
# Runtime 执行时已 cd 到本服务目录，所以 $PWD 就是 ./services/motion
# ============================================================================
set -e

SERVICE_DIR="$(cd "$(dirname "$0")" && pwd)"
ROS2_WS_DIR="$SERVICE_DIR/../../ros2_ws"

echo "[motion] Starting motion service..."

# 1) source ROS2 环境
source /opt/ros/humble/setup.bash

# 2) source 工作空间
if [ -f "$ROS2_WS_DIR/install/setup.bash" ]; then
    source "$ROS2_WS_DIR/install/setup.bash"
fi

# 3) 启动 ROS2 launch
cd "$ROS2_WS_DIR"
ros2 launch module_manager_hub manager.launch.py
