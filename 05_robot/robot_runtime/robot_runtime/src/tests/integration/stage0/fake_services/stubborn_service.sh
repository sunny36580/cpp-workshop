#!/usr/bin/env bash
# fake_stubborn_service — 模拟不响应 SIGTERM 的服务
NAME="${1:-fake_stubborn}"
echo "[$NAME] started (PID=$$), ignoring SIGTERM"
trap "" SIGTERM
while true; do sleep 1; done
