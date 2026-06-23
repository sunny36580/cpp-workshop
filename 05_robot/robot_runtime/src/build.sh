#!/bin/bash
# ============================================================================
# Robot Runtime — 一键编译 + 打包
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=========================================="
echo " Robot Runtime — Build"
echo "=========================================="

# ---- 编译 ----
echo ""
echo "[1/2] Building..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "[1/2] binary → $BUILD_DIR/robot"

# ---- 打包到 install/ ----
echo ""
echo "[2/2] Packaging to $SCRIPT_DIR/install..."
rm -rf "$SCRIPT_DIR/install"
cmake --install "$BUILD_DIR" --prefix "$SCRIPT_DIR/install"
echo "[2/2] install/ ready"

echo ""
echo "=========================================="
echo " Done!"
echo "  binary:  $BUILD_DIR/robot"
echo "  deploy:  $SCRIPT_DIR/install/"
echo "  Usage:   ./start_runtime.sh list"
echo "=========================================="
