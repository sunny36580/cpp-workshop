#!/usr/bin/env bash
# fake_crash_service — 模拟崩溃服务，启动后立刻退出
echo "[${1:-fake_crash}] started, crashing immediately"
exit 1
