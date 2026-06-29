#!/bin/bash
# ============================================================================
# 环境初始化脚本
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
echo "[env_setup] Setting up environment in $SCRIPT_DIR"

# 安装 yaml-cpp（如未安装）
if ! dpkg -l libyaml-cpp-dev &>/dev/null; then
    echo "[env_setup] Installing yaml-cpp..."
    sudo apt-get update && sudo apt-get install -y libyaml-cpp-dev
fi

# 创建必要目录
mkdir -p "$SCRIPT_DIR/build"
mkdir -p "$SCRIPT_DIR/log/services"
mkdir -p "$SCRIPT_DIR/config"
mkdir -p "$SCRIPT_DIR/modes"

echo "[env_setup] Done"
