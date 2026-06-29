#!/usr/bin/env bash
# ============================================================================
# Robot Runtime — 一键编译 + 打包
# ============================================================================
set -e

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

echo "=========================================="
echo " Robot Runtime — Build"
echo "=========================================="

# ---- 编译 ----
echo ""
echo "[1/2] Building..."
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCMAKE_INSTALL_PREFIX="$ROOT_DIR/install"
cmake --build "$BUILD_DIR" -j"$(nproc)"

# ---- 打包到 install/ ----
echo ""
echo "[2/2] Packaging to $ROOT_DIR/install..."
rm -rf "$ROOT_DIR/install"
cmake --install "$BUILD_DIR"
echo "[2/2] install/ ready"

echo ""
echo "=========================================="
echo " Done!"
echo "  binary:  $BUILD_DIR/src/robot"
echo "  deploy:  $ROOT_DIR/install/"
echo "  Usage:   ./start_runtime.sh list"
echo "=========================================="
