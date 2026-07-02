# ============================================================================
# Robot Runtime — 单元测试
# 每个模块一个 test executable，链接对应 lib，不重复编译业务源码。
# 暂不测 gateway/tcp、gateway/cli（偏集成）。
# ============================================================================

# --------------------------------------------------------------------------
# runtime_unit_tests
# --------------------------------------------------------------------------
add_executable(runtime_unit_tests
    unit/runtime/dependency_manager_test.cpp
    unit/runtime/service_manager_test.cpp
)
target_link_libraries(runtime_unit_tests PRIVATE
    runtime_lib
    GTest::gtest_main
)
target_include_directories(runtime_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(runtime_unit_tests)

# --------------------------------------------------------------------------
# orchestration_unit_tests
# --------------------------------------------------------------------------
add_executable(orchestration_unit_tests
    unit/orchestration/mode_manager_test.cpp
)
target_link_libraries(orchestration_unit_tests PRIVATE
    orchestration_lib
    GTest::gtest_main
)
target_include_directories(orchestration_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(orchestration_unit_tests)

# --------------------------------------------------------------------------
# common_unit_tests
# --------------------------------------------------------------------------
add_executable(common_unit_tests
    unit/common/config_loader_test.cpp
)
target_link_libraries(common_unit_tests PRIVATE
    common_lib
    GTest::gtest_main
)
target_include_directories(common_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(common_unit_tests)

# ============================================================================
# 阶段 0 集成测试 — 用 fake service 验证 Runtime 核心管控能力
# ============================================================================

# stage0_smoke
add_executable(stage0_smoke_test
    integration/stage0/stage0_smoke_test.cpp
)
target_link_libraries(stage0_smoke_test PRIVATE
    common_lib runtime_lib
    GTest::gtest_main
)
target_include_directories(stage0_smoke_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(stage0_smoke_test)

# stage0_service_lifecycle
add_executable(stage0_lifecycle_test
    integration/stage0/stage0_service_lifecycle_test.cpp
)
target_link_libraries(stage0_lifecycle_test PRIVATE
    runtime_lib
    GTest::gtest_main
)
target_include_directories(stage0_lifecycle_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(stage0_lifecycle_test)

# stage0_dependency_order
add_executable(stage0_dep_test
    integration/stage0/stage0_dependency_order_test.cpp
)
target_link_libraries(stage0_dep_test PRIVATE
    runtime_lib
    GTest::gtest_main
)
target_include_directories(stage0_dep_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(stage0_dep_test)

# stage0_mode_switch
add_executable(stage0_mode_test
    integration/stage0/stage0_mode_switch_test.cpp
)
target_link_libraries(stage0_mode_test PRIVATE
    runtime_lib orchestration_lib
    GTest::gtest_main
)
target_include_directories(stage0_mode_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(stage0_mode_test)

# stage0_recovery
add_executable(stage0_rec_test
    integration/stage0/stage0_recovery_test.cpp
)
target_link_libraries(stage0_rec_test PRIVATE
    runtime_lib
    GTest::gtest_main
)
target_include_directories(stage0_rec_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(stage0_rec_test)
