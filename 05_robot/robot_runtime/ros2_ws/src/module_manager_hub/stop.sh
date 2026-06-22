#!/bin/bash
# ============================================================================
# module_manager_hub — 模块管理器停止脚本
# ============================================================================
set -e

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_NAME="module_manager_node"

echo "[module_manager_hub] 停止中..."

# 1) 尝试通过 ros2 lifecycle 或 service 停止（如果有）
# 2) 直接 pkill 进程（主进程含三个 node）
pkill -f "$SERVICE_NAME" 2>/dev/null || true

# 等待进程退出
sleep 1
if pgrep -f "$SERVICE_NAME" > /dev/null 2>&1; then
    echo "[module_manager_hub] 强制终止..."
    pkill -9 -f "$SERVICE_NAME" 2>/dev/null || true
fi

echo "[module_manager_hub] 已停止"
