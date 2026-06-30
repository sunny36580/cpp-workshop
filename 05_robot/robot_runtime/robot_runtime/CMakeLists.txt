cmake_minimum_required(VERSION 3.16)
project(robot_runtime VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)

include(CTest)
option(BUILD_TESTING "Build unit tests" ON)

# ============================================================================
# GoogleTest (system-installed)
# ============================================================================
if(BUILD_TESTING)
    find_package(GTest REQUIRED)
endif()

# ============================================================================
# 源码编译
# ============================================================================
add_subdirectory(src)

# ============================================================================
# Install — 打包到 install/ 目录，colcon 风格
# ============================================================================
# 编译后执行:
#   cmake --install build --prefix install
# 产物:
#   install/
#   ├── bin/robot
#   ├── lib/libruntime_lib.a  lib/liborchestration_lib.a  lib/libgateway_lib.a
#   ├── config/*.yaml          ← 含 modes/ 子目录
#   ├── services/              ← 整个目录，含脚本、配置、Python 源码等
#   ├── tools/*.sh
#   ├── setup.bash
#   ├── local_setup.bash
#   └── start_runtime.sh
# ============================================================================

# 配置文件
install(DIRECTORY config/ DESTINATION config
    FILES_MATCHING PATTERN "*.yaml"
)

# 辅助工具
install(DIRECTORY tools/ DESTINATION tools
    FILES_MATCHING PATTERN "*.sh"
)

# 启动入口
install(FILES start_runtime.sh DESTINATION .
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

# 环境设置
install(FILES setup.bash local_setup.bash DESTINATION .)

# 服务目录 — 整个拷贝（每个服务是一个完整 包 + start/stop.sh）
install(DIRECTORY services/ DESTINATION services
    PATTERN "*.pyc" EXCLUDE
    PATTERN "__pycache__" EXCLUDE
)
