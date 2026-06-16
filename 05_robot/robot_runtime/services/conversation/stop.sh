#!/bin/bash
# ============================================================================
# Conversation Service — 对话服务停止脚本
# ============================================================================
set -e

echo "[conversation] Stopping conversation service..."

pkill -f "handshake_server.py" 2>/dev/null || true
pkill -f "action_group_server.py" 2>/dev/null || true

echo "[conversation] Conversation service stopped"
