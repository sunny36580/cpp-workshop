#!/usr/bin/env bash
# fake_slow_service — 模拟慢启动服务，等几秒后才真正运行
NAME="${1:-fake_slow}"
echo "[$NAME] starting slowly..."
sleep 3
echo "[$NAME] started (PID=$$)"
trap "echo \"[$NAME] stopped\"; exit 0" SIGTERM SIGINT
while true; do sleep 1; done
