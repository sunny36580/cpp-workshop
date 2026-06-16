cmake_minimum_required(VERSION 3.16)
project(robot_runtime VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# 查找依赖
# ============================================================================
find_package(yaml-cpp REQUIRED)

# ============================================================================
# 源文件 — src/ 和 include/ 严格对应
# ============================================================================
set(RUNTIME_SOURCES
    src/core/service.cpp
    src/core/service_manager.cpp
    src/managers/mode_manager.cpp
    src/managers/monitor_manager.cpp
)

# common 工具类（头文件 only，无 .cpp 文件）
set(RUNTIME_PUBLIC_HEADERS
    include/runtime/common/log.h
    include/runtime/common/process_manager.h
    include/runtime/common/config_parser.h
    include/runtime/core/service.h
    include/runtime/core/service_manager.h
    include/runtime/managers/mode_manager.h
    include/runtime/managers/monitor_manager.h
    include/runtime/plugin/iplugin.h
)

# ============================================================================
# Runtime 静态库
# ============================================================================
add_library(runtime STATIC ${RUNTIME_SOURCES} ${RUNTIME_PUBLIC_HEADERS})
target_include_directories(runtime PUBLIC include)
target_link_libraries(runtime PUBLIC yaml-cpp)

# ============================================================================
# CLI 可执行文件
# ============================================================================
add_executable(robot cli/main.cpp)
target_link_libraries(robot runtime)
target_include_directories(robot PRIVATE include)

# ============================================================================
# Install
# ============================================================================
install(TARGETS robot runtime
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)
