# ============================================================================
# Robot Runtime — 仅本包的局部环境设置
# ============================================================================
# 与 setup.bash 内容一致，colcon 约定局部 setup 文件
# ============================================================================
if [ -n "${BASH_SOURCE[0]}" ]; then
    _SETUP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
else
    _SETUP_DIR="$(cd "$(dirname "$0")" && pwd)"
fi

if [ -d "$_SETUP_DIR/bin" ]; then
    export PATH="$_SETUP_DIR/bin:$PATH"
fi

if [ -d "$_SETUP_DIR/lib" ]; then
    export LD_LIBRARY_PATH="$_SETUP_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

echo "[robot_runtime] sourced $_SETUP_DIR/local_setup.bash"
unset _SETUP_DIR
