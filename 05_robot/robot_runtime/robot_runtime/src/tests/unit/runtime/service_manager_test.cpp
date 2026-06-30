#include <gtest/gtest.h>

#include "runtime/process/service_manager/service_manager.h"

#include <fstream>
#include <cstdio>

using robot_runtime::ServiceManager;
using robot_runtime::ProcessService;
using robot_runtime::ServiceState;

// ============================================================================
// 辅助：创建临时 services.yaml
// ============================================================================
static std::string create_services_yaml(const std::string& dir) {
    auto path = dir + "/services.yaml";
    std::ofstream f(path);
    f << R"(
services:
  lidar_driver:
    path: ./services/lidar_driver
    description: 激光雷达驱动
    type: ros2
    launch_cmd: ros2 launch lidar_driver driver.launch.py
    depends: []
    auto_restart: true

  slam:
    path: ./services/slam
    description: SLAM 定位
    type: ros2
    launch_cmd: ros2 launch slam slam.launch.py
    depends:
      - lidar_driver
    auto_restart: false

  navigation:
    path: ./services/navigation
    description: 导航规划
    type: ros2
    launch_cmd: ros2 launch navigation nav.launch.py
    depends:
      - slam
    auto_restart: false
)";
    return path;
}

// ============================================================================
// ServiceManager
// ============================================================================

TEST(ServiceManagerTest, Construct) {
    ServiceManager sm(".", "/tmp", "/tmp");
    SUCCEED();
}

TEST(ServiceManagerTest, LoadConfigLoadsServices) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    create_services_yaml(tmp_dir);

    ServiceManager sm(".", tmp_dir, "/tmp");
    EXPECT_TRUE(sm.load_config("services.yaml"));

    auto all = sm.all_status();
    EXPECT_EQ(all.size(), 3);

    // cleanup
    std::remove((std::string(tmp_dir) + "/services.yaml").c_str());
    std::remove(tmp_dir);
}

TEST(ServiceManagerTest, LoadNonexistentConfigThrows) {
    ServiceManager sm(".", "/tmp", "/tmp");
    // parse_services 内部调用 YAML::LoadFile，文件不存在时抛异常
    EXPECT_THROW(sm.load_config("nonexistent.yaml"), YAML::Exception);
}

TEST(ServiceManagerTest, RegisterAndGetService) {
    ServiceManager sm(".", "/tmp", "/tmp");
    auto svc = std::make_shared<ProcessService>("test_svc", "/tmp");
    sm.register_service(svc);

    auto got = sm.get("test_svc");
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->name(), "test_svc");
}

TEST(ServiceManagerTest, GetNonExistentReturnsNull) {
    ServiceManager sm(".", "/tmp", "/tmp");
    EXPECT_EQ(sm.get("nonexistent"), nullptr);
}

TEST(ServiceManagerTest, StartNonExistentServiceReturnsFalse) {
    ServiceManager sm(".", "/tmp", "/tmp");
    EXPECT_FALSE(sm.start("nonexistent"));
}

TEST(ServiceManagerTest, StopNonExistentServiceReturnsFalse) {
    ServiceManager sm(".", "/tmp", "/tmp");
    EXPECT_FALSE(sm.stop("nonexistent"));
}

TEST(ServiceManagerTest, ResolveStartOrderMatchesDependencies) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    create_services_yaml(tmp_dir);

    ServiceManager sm(".", tmp_dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));

    auto order = sm.resolve_start_order();
    ASSERT_GE(order.size(), 3);
    // lidar_driver → slam → navigation
    auto posLidar = std::find(order.begin(), order.end(), "lidar_driver");
    auto posSlam  = std::find(order.begin(), order.end(), "slam");
    auto posNav   = std::find(order.begin(), order.end(), "navigation");
    EXPECT_LT(posLidar, posSlam);
    EXPECT_LT(posSlam, posNav);

    std::remove((std::string(tmp_dir) + "/services.yaml").c_str());
    std::remove(tmp_dir);
}
