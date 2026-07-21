#!/bin/bash
# ============================================================================
# module_manager_hub — 模块管理器启动脚本
# 同一进程内运行：SerialJoyBridge + CameraStreamer
# ============================================================================
set -e

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

# source 同级目录下的 colcon 生成的 setup.sh
if [ -f "$SELF_DIR/setup.sh" ]; then
    COLCON_CURRENT_PREFIX=$SELF_DIR source "$SELF_DIR/setup.sh"
else
    echo "[module_manager_hub] 警告: 未找到 $SELF_DIR/setup.sh，请先 source workspace"
fi

# cmake --install 拷贝时可能丢失执行权限，启动前确保二进制可执行
find "$SELF_DIR/module_manager_hub" -type f \( -name "module_manager_node" \
    -o -name "camera_streamer_node" -o -name "joy_bridge_node" \
    -o -name "heartbeat_collector_node" -o -name "*.so" \) \
    -exec chmod +x {} \; 2>/dev/null

echo "[module_manager_hub] 启动中..."
ros2 launch module_manager_hub manager.launch.py
