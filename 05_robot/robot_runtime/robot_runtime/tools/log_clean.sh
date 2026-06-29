#!/bin/bash
# ============================================================================
# 日志清理脚本
# ============================================================================
LOG_DIR="$(cd "$(dirname "$0")/../log" && pwd)"
DAYS=${1:-7}

echo "[log_clean] Cleaning logs older than $DAYS days in $LOG_DIR"
find "$LOG_DIR" -name "*.log" -type f -mtime +"$DAYS" -delete 2>/dev/null || true
echo "[log_clean] Done"
