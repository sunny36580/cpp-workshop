#!/bin/bash
# ============================================================================
# module_manager_hub — 模块管理器启动脚本
# 同一进程内运行三个 node（ModuleManager + SerialJoyBridge + CameraStreamer）
# ============================================================================
set -e

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SELF_DIR"

# 向上找 setup.sh（install/module_manager_hub/ → install/）
PARENT_DIR="$(cd "$SELF_DIR/.." && pwd)"
if [ -f "$PARENT_DIR/setup.sh" ]; then
    COLCON_CURRENT_PREFIX=$SELF_DIR source "$PARENT_DIR/setup.sh"
else
    echo "[module_manager_hub] 警告: 未找到 $PARENT_DIR/setup.sh，请先 source workspace"
fi

echo "[module_manager_hub] 启动中..."
ros2 launch module_manager_hub manager.launch.py