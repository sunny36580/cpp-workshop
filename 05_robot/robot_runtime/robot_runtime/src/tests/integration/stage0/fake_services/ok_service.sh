#!/usr/bin/env bash
# fake_ok_service — 模拟正常服务，运行直到收到 SIGTERM
set -e

NAME="${1:-fake_ok}"
echo "[$NAME] started (PID=$$)"
trap "echo \"[$NAME] stopped\"; exit 0" SIGTERM SIGINT

while true; do
    sleep 1
done
