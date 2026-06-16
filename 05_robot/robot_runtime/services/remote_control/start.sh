#!/bin/bash
# ============================================================================
# Remote Control Service — 远程遥控服务启动脚本
# ============================================================================
# PC 端遥控界面（图传 + 手柄 + 串口转发）
# Runtime 执行时已 cd 到本服务目录
# ============================================================================
set -e

SERVICE_DIR="$(cd "$(dirname "$0")" && pwd)"
ROS2_WS_DIR="$SERVICE_DIR/../../ros2_ws"

echo "[remote_control] Starting remote control service..."

# 1) 激活 Python 虚拟环境（如果存在）
if [ -f "$SERVICE_DIR/../../venv/bin/activate" ]; then
    source "$SERVICE_DIR/../../venv/bin/activate"
fi

# 2) 启动遥控客户端（前台）
exec python3 "$ROS2_WS_DIR/src/remote_control_client/remote_control_client.py"
