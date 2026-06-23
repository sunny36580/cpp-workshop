cmake_minimum_required(VERSION 3.16)
project(robot_runtime VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)

# ============================================================================
# 源文件
# ============================================================================
set(RUNTIME_SOURCES
    core/runtime.cpp
    manager/service_manager/service_manager.cpp
    manager/mode_manager/mode_manager.cpp
    manager/monitor_manager/monitor_manager.cpp
    manager/monitor_manager/detector/file_heartbeat_detector.cpp
    manager/dependency_manager/dependency_manager.cpp
    network/tcp_server.cpp
)

set(RUNTIME_PUBLIC_HEADERS
    common/type_def.h
    common/log.h
    common/config_loader.h
    common/process_utils.h
    manager/service_manager/service_manager.h
    manager/mode_manager/mode_manager.h
    manager/monitor_manager/monitor_manager.h
    manager/dependency_manager/dependency_manager.h
    manager/dependency_manager/topological_sort.h
    core/runtime.h
    network/tcp_server.h
    network/tcp_server.cpp
    network/protocol_parser.h
    network/command_dispatcher.h
    network/auth_manager.h
)

add_library(runtime STATIC ${RUNTIME_SOURCES} ${RUNTIME_PUBLIC_HEADERS})
target_include_directories(runtime PUBLIC .)
target_link_libraries(runtime PUBLIC yaml-cpp pthread)

add_executable(robot
    cli/main.cpp
    cli/cmd_service.h
    cli/cmd_mode.h
    cli/cmd_monitor.h
)
target_link_libraries(robot runtime)
target_include_directories(robot PRIVATE .)

# ============================================================================
# Install — 打包到 install/ 目录，colcon 风格
# ============================================================================
# 编译后执行:
#   cmake --install build --prefix install
# 产物:
#   install/
#   ├── bin/robot
#   ├── lib/libruntime.a
#   ├── config/*.yaml
#   ├── services/*/start.sh stop.sh
#   ├── modes/*.yaml
#   ├── tools/*.sh
#   ├── setup.bash
#   └── start_runtime.sh
# ============================================================================

# ============================================================================
# Install — 打包到 install/ 目录，colcon 风格
# ============================================================================
# 编译后执行:
#   cmake --install build --prefix install
# 产物:
#   install/
#   ├── bin/robot
#   ├── lib/libruntime.a
#   ├── config/*.yaml
#   ├── services/          ← 整个目录，含脚本、配置、Python 源码等
#   ├── modes/*.yaml
#   ├── tools/*.sh
#   ├── setup.bash
#   ├── local_setup.bash
#   └── start_runtime.sh
# ============================================================================

install(TARGETS robot runtime
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# 配置文件
install(DIRECTORY config/ DESTINATION config
    FILES_MATCHING PATTERN "*.yaml"
)

# 服务目录 — 整个拷贝（每个服务是一个完整 包 + start/stop.sh）
install(DIRECTORY services/ DESTINATION services
    PATTERN "*.pyc" EXCLUDE
    PATTERN "__pycache__" EXCLUDE
)

# 模式配置
install(DIRECTORY modes/ DESTINATION modes
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

install(DIRECTORY config/ DESTINATION config
    FILES_MATCHING PATTERN "*.yaml"
)

install(DIRECTORY services/ DESTINATION services
    FILES_MATCHING PATTERN "*.sh"
)

install(DIRECTORY modes/ DESTINATION modes
    FILES_MATCHING PATTERN "*.yaml"
)

install(DIRECTORY tools/ DESTINATION tools
    FILES_MATCHING PATTERN "*.sh"
)

install(FILES start_runtime.sh DESTINATION .
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

install(FILES
    setup.bash
    local_setup.bash
DESTINATION .
)
