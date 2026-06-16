#!/bin/bash
# ============================================================================
# Conversation Service — 对话服务启动脚本
# ============================================================================
# 启动 WebSocket 服务端：handshake_server (9999) + action_group_server (9998)
# Runtime 执行时已 cd 到本服务目录
# ============================================================================
set -e

SERVICE_DIR="$(cd "$(dirname "$0")" && pwd)"
ROS2_WS_DIR="$SERVICE_DIR/../../ros2_ws"
SRC_DIR="$ROS2_WS_DIR/src/remote_control_client"

echo "[conversation] Starting conversation service..."

# 1) 激活 Python 虚拟环境（如果存在）
if [ -f "$SERVICE_DIR/../../venv/bin/activate" ]; then
    source "$SERVICE_DIR/../../venv/bin/activate"
fi

# 2) 启动 handshake server（后台）
python3 "$SRC_DIR/handshake_server.py" &
HANDSHAKE_PID=$!
echo "[conversation] handshake_server PID=$HANDSHAKE_PID"

# 3) 启动 action group server（前台，保持进程存活）
exec python3 "$SRC_DIR/action_group_server.py"
