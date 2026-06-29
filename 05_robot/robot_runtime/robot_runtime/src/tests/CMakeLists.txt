# ============================================================================
# Robot Runtime — 单元测试
# 每个模块一个 test executable，链接对应 lib，不重复编译业务源码。
# ============================================================================

# --------------------------------------------------------------------------
# runtime_unit_tests
# --------------------------------------------------------------------------
add_executable(runtime_unit_tests
    unit/runtime/dependency_manager_test.cpp
    unit/runtime/file_heartbeat_detector_test.cpp
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
# gateway_unit_tests
# --------------------------------------------------------------------------
add_executable(gateway_unit_tests
    unit/gateway/protocol_parser_test.cpp
    unit/gateway/command_dispatcher_test.cpp
)
target_link_libraries(gateway_unit_tests PRIVATE
    gateway_lib
    GTest::gtest_main
)
target_include_directories(gateway_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
gtest_discover_tests(gateway_unit_tests)
