#!/bin/bash
# ============================================================================
# Robot Runtime — 一键编译脚本
# ============================================================================
# 统一编译所有组件：
#   - runtime/      → build/runtime/  (cmake)
#   - ros2_ws/      → ros2_ws/build/install/  (colcon)
#   - plugins/      → build/plugins/  (Phase6 启用)
# ============================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
ROS2_WS_DIR="$SCRIPT_DIR/ros2_ws"

echo "=========================================="
echo " Robot Runtime — Build All"
echo "=========================================="

# ---- 1. 编译 runtime 框架 ----
echo ""
echo "[1/2] Building runtime..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "[1/2] runtime → $BUILD_DIR/robot"

# ---- 2. 编译 ROS2 服务 ----
echo ""
echo "[2/2] Building ROS2 services (colcon)..."
if command -v colcon &> /dev/null; then
    if [ -d "$ROS2_WS_DIR/src/module_manager_hub" ]; then
        # source ROS2 环境
        if [ -f /opt/ros/humble/setup.bash ]; then
            source /opt/ros/humble/setup.bash
        fi

        cd "$ROS2_WS_DIR"
        colcon build \
            --packages-select module_manager_hub \
            --cmake-args -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE:-Release} \
            --parallel-workers "$(nproc)"

        echo "[2/2] ROS2 services → $ROS2_WS_DIR/install/"
    else
        echo "[2/2] No ROS2 packages found in $ROS2_WS_DIR/src/, skipping."
    fi
else
    echo "[2/2] 'colcon' not found, skipping ROS2 services."
    echo "    Install ROS2 humble first, or run: sudo apt install python3-colcon-common-extensions"
fi

echo ""
echo "=========================================="
echo " Build complete!"
echo "  runtime:  $BUILD_DIR/robot"
echo "  ROS2:     $ROS2_WS_DIR/install/"
echo "  Usage:    ./start_runtime.sh up"
echo "=========================================="
