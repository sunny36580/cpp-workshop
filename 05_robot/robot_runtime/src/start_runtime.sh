#!/bin/bash
# ============================================================================
# Robot Runtime — 启动入口
# ============================================================================
# 统一配置工作目录、配置路径、日志路径，避免每次敲命令都带一堆参数。
# 用法:
#   ./start_runtime.sh status             查看状态
#   ./start_runtime.sh up                 启动默认模式
#   ./start_runtime.sh mode switch teleop 切换模式
#   ./start_runtime.sh start motion       启动单个服务
#   ./start_runtime.sh build              仅编译，不启动
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 如果第一个参数是 build，只编译不启动
if [ "$1" = "build" ]; then
    exec "$SCRIPT_DIR/build.sh"
fi

# 确保 runtime 已编译
RUNTIME_BIN="$SCRIPT_DIR/build/robot"
if [ ! -f "$RUNTIME_BIN" ]; then
    echo "[start_runtime] Runtime not built, running build.sh first..."
    "$SCRIPT_DIR/build.sh"
fi

# 统一参数：工作目录、配置目录、日志目录
exec "$RUNTIME_BIN" \
    -w "$SCRIPT_DIR" \
    -c "$SCRIPT_DIR/config" \
    -l "$SCRIPT_DIR/log" \
    "$@"
