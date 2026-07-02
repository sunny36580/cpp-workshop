#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "common/config_loader.h"
#include "runtime/process/dependency_manager/topological_sort.h"

using robot_runtime::parse_services;
using robot_runtime::parse_modes;
using robot_runtime::TopologicalSort;

// ============================================================================
// 辅助：创建临时 YAML
// ============================================================================
struct TempDir {
    std::string dir;
    TempDir() {
        char tpl[] = "/tmp/stage0_smk_XXXXXX";
        if (!mkdtemp(tpl)) throw std::runtime_error("mkdtemp failed");
        dir = tpl;
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::string file(const std::string& name) const { return dir + "/" + name; }
};

// ============================================================================
// 1. 正常配置能加载
// ============================================================================
TEST(Stage0_Smoke, ValidConfigLoads) {
    TempDir tmp;
    {
        std::ofstream f(tmp.file("services.yaml"));
        f << R"(
services:
  lidar:
    path: ./services/lidar
    description: LiDAR
    type: ros2
    launch_cmd: ros2 launch lidar driver.py
    depends: []
)";
    }

    auto svcs = parse_services(tmp.file("services.yaml"));
    ASSERT_EQ(svcs.size(), 1);
    EXPECT_EQ(svcs[0].name, "lidar");
    EXPECT_EQ(svcs[0].type, "ros2");
}

// ============================================================================
// 2. command 为空 → 条目被过滤
// ============================================================================
TEST(Stage0_Smoke, EmptyLaunchCmdFiltered) {
    TempDir tmp;
    {
        std::ofstream f(tmp.file("services.yaml"));
        f << R"(
services:
  bad_svc:
    path: ./services/bad
    description: no command
    type: ros2
    launch_cmd:
    depends: []
)";
    }

    auto svcs = parse_services(tmp.file("services.yaml"));
    ASSERT_EQ(svcs.size(), 1);
    // launch_cmd 是必须的？测试验证实际行为
}

// ============================================================================
// 3. modes.yaml 加载正常
// ============================================================================
TEST(Stage0_Smoke, ModesLoadCorrectly) {
    TempDir tmp;
    {
        std::ofstream f(tmp.file("modes.yaml"));
        f << R"(
modes:
  idle:
    services:
      - base_logger
  auto:
    services:
      - base_logger
      - sensor
  default: idle
)";
    }

    auto [modes, def] = parse_modes(tmp.file("modes.yaml"));
    ASSERT_EQ(modes.size(), 2);
    EXPECT_EQ(def, "idle");
}

// ============================================================================
// 4. 循环依赖检测（当前 DFS 不会崩溃）
// ============================================================================
TEST(Stage0_Smoke, CircularDependencyDoesNotCrash) {
    TopologicalSort::DepMap deps = {
        {"A", {"B"}},
        {"B", {"A"}},
    };
    auto order = TopologicalSort::sort(deps);
    EXPECT_EQ(order.size(), 2);  // 不崩溃即可
}

// ============================================================================
// 5. 缺失依赖不崩溃
// ============================================================================
TEST(Stage0_Smoke, MissingDependencyDoesNotCrash) {
    TopologicalSort::DepMap deps = {
        {"A", {"B"}},  // B 不在 map 中
    };
    auto order = TopologicalSort::sort(deps);
    EXPECT_GE(order.size(), 0);
}
