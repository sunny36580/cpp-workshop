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
