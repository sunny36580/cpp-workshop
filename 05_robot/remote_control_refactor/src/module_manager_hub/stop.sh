#!/bin/bash
# ============================================================================
# module_manager_hub — 模块管理器停止脚本
# 确保无僵尸/孤儿进程残留
#
# 关键点：
#   - 微轮询（0.1s 间隔），进程退出后立即返回，不浪费等待时间
#   - 优先 SIGTERM 让 ROS2 完成 rclcpp::shutdown() → DDS 资源释放
#   - SIGKILL 仅作为超时后的最后手段
#   - 最后停掉 ROS2 daemon（daemon 本身也是一种进程残留）
# ============================================================================
set -e

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVICE_NAME="module_manager_node"

echo "[module_manager_hub] 停止中..."

if ! pgrep -f "$SERVICE_NAME" > /dev/null 2>&1; then
    echo "[module_manager_hub] 未发现运行中的进程"
else
    # 1) SIGTERM 优雅停止
    echo "[module_manager_hub] 发送 SIGTERM..."
    pkill -f "$SERVICE_NAME" 2>/dev/null || true

    # 微轮询：每 0.1s 检查一次，最多等 2 秒
    # 进程通常几百毫秒内就退出了，立即能检测到
    for _ in $(seq 1 20); do
        if ! pgrep -f "$SERVICE_NAME" > /dev/null 2>&1; then
            echo "[module_manager_hub] 进程已退出"
            break
        fi
        sleep 0.1
    done

    # 2) 超时未退出 → 最后手段
    if pgrep -f "$SERVICE_NAME" > /dev/null 2>&1; then
        echo "[module_manager_hub] 强制终止..."
        pkill -9 -f "$SERVICE_NAME" 2>/dev/null || true
        sleep 0.5
    fi
fi

# 3) 清理 ROS2 daemon（避免 daemon 进程占用 DDS 资源）
if command -v ros2 &>/dev/null; then
    ros2 daemon stop 2>/dev/null || true
fi

# 4) 最终确认
if pgrep -f "$SERVICE_NAME" > /dev/null 2>&1; then
    echo "[module_manager_hub] 错误: 仍有残留进程"
    exit 1
fi

echo "[module_manager_hub] 已停止，无残留进程"
