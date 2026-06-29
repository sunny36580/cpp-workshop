#!/bin/bash
# ============================================================================
# Robot Runtime — 启动入口
# ============================================================================
# 支持: 源码目录 ./start_runtime.sh list
#       安装目录 /opt/robot/start_runtime.sh list
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 自动检测目录布局：源码树 vs 安装树
if [ -f "$SCRIPT_DIR/build/robot" ]; then
    RUNTIME_BIN="$SCRIPT_DIR/build/robot"
    CONFIG_DIR="$SCRIPT_DIR/config"
    LOG_DIR="$SCRIPT_DIR/log"

    # 源码树支持 build 命令
    if [ "$1" = "build" ]; then
        exec "$SCRIPT_DIR/build.sh"
    fi
elif [ -f "$SCRIPT_DIR/bin/robot" ]; then
    RUNTIME_BIN="$SCRIPT_DIR/bin/robot"
    CONFIG_DIR="$SCRIPT_DIR/config"
    LOG_DIR="$SCRIPT_DIR/log"
else
    echo "[start_runtime] robot binary not found!"
    echo "  Source tree: run './build.sh' first"
    echo "  Install:     run 'cmake --install build --prefix install'"
    exit 1
fi

mkdir -p "$LOG_DIR"
mkdir -p "$LOG_DIR/services"

exec "$RUNTIME_BIN" \
    -w "$SCRIPT_DIR" \
    -c "$CONFIG_DIR" \
    -l "$LOG_DIR" \
    "$@"
