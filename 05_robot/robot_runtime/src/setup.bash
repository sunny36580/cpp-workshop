# ============================================================================
# Robot Runtime — 环境设置（colcon 风格）
# ============================================================================
# 用法: source setup.bash
# 作用: 将 install/bin 加入 PATH，方便直接执行 robot 命令
# ============================================================================

# 获取本脚本所在目录（支持 source 和直接执行两种方式）
if [ -n "${BASH_SOURCE[0]}" ]; then
    _SETUP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
else
    _SETUP_DIR="$(cd "$(dirname "$0")" && pwd)"
fi

# 添加 bin 到 PATH
if [ -d "$_SETUP_DIR/bin" ]; then
    export PATH="$_SETUP_DIR/bin:$PATH"
fi

if [ -d "$_SETUP_DIR/lib" ]; then
    export LD_LIBRARY_PATH="$_SETUP_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

echo "[robot_runtime] sourced $_SETUP_DIR/setup.bash"
echo "  robot command available. Run 'robot help' for usage."

unset _SETUP_DIR
