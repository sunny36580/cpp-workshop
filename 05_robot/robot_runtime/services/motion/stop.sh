#!/bin/bash
# ============================================================================
# Motion Service — 运控服务停止脚本
# ============================================================================
set -e

echo "[motion] Stopping motion service..."

pkill -f "module_manager_node" 2>/dev/null || true
pkill -f "serial_joy_bridge_node" 2>/dev/null || true
pkill -f "camera_streamer_node" 2>/dev/null || true

echo "[motion] Motion service stopped"
