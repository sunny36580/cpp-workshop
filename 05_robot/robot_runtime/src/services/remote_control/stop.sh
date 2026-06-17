#!/bin/bash
# ============================================================================
# Remote Control Service — 远程遥控服务停止脚本
# ============================================================================
set -e

echo "[remote_control] Stopping remote control service..."

pkill -f "remote_control_client.py" 2>/dev/null || true

echo "[remote_control] Remote control service stopped"
