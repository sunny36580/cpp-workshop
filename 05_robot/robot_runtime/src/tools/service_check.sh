#!/bin/bash
# ============================================================================
# 服务批量自检脚本
# ============================================================================
SCRIPT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
RUNTIME_BIN="$SCRIPT_DIR/build/robot"

if [ ! -f "$RUNTIME_BIN" ]; then
    echo "[service_check] Runtime not built. Run build.sh first."
    exit 1
fi

echo "=========================================="
echo " Service Self-Check"
echo "=========================================="

# 1) 列出所有服务
echo ""
echo "[1] Registered services:"
"$RUNTIME_BIN" -w "$SCRIPT_DIR" -c "$SCRIPT_DIR/config" -l "$SCRIPT_DIR/log" list

# 2) 检查状态
echo "[2] Current status:"
"$RUNTIME_BIN" -w "$SCRIPT_DIR" -c "$SCRIPT_DIR/config" -l "$SCRIPT_DIR/log" status

echo ""
echo "=========================================="
echo " Check complete"
echo "=========================================="
