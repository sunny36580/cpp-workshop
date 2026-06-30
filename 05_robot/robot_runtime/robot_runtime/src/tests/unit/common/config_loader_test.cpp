#include <gtest/gtest.h>

#include "common/config_loader.h"

#include <fstream>
#include <cstdio>

using robot_runtime::parse_services;
using robot_runtime::parse_modes;
using robot_runtime::ServiceConfig;

// ============================================================================
// 辅助：创建临时 YAML 文件
// ============================================================================
static std::string create_valid_services_yaml(const std::string& dir) {
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
)";
    return path;
}

static std::string create_valid_modes_yaml(const std::string& dir) {
    auto path = dir + "/modes.yaml";
    std::ofstream f(path);
    f << R"(
modes:
  standby:
    services:
      - motion
  teleop:
    services:
      - motion
      - remote_control
  default: teleop
)";
    return path;
}

static std::string create_missing_fields_yaml(const std::string& dir) {
    auto path = dir + "/bad_services.yaml";
    std::ofstream f(path);
    f << R"(
services:
  incomplete_entry:
    description: missing path and type
)";
    return path;
}

// ============================================================================
// parse_services
// ============================================================================

TEST(ConfigLoaderTest, ParseValidServices) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    auto path = create_valid_services_yaml(tmp_dir);

    auto services = parse_services(path);
    ASSERT_EQ(services.size(), 2);

    EXPECT_EQ(services[0].name, "lidar_driver");
    EXPECT_EQ(services[0].type, "ros2");
    EXPECT_TRUE(services[0].auto_restart);
    EXPECT_EQ(services[0].depends.size(), 0);

    EXPECT_EQ(services[1].name, "slam");
    EXPECT_EQ(services[1].depends.size(), 1);
    EXPECT_EQ(services[1].depends[0], "lidar_driver");

    std::remove(path.c_str());
    std::remove(tmp_dir);
}

TEST(ConfigLoaderTest, ParseNonexistentFileThrows) {
    EXPECT_THROW(parse_services("/tmp/nonexistent_XXXX.yaml"), YAML::Exception);
}

TEST(ConfigLoaderTest, ParseMissingServicesKeyReturnsEmpty) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    auto path = std::string(tmp_dir) + "/empty.yaml";
    { std::ofstream f(path); f << "other_key: value\n"; }

    auto services = parse_services(path);
    EXPECT_TRUE(services.empty());

    std::remove(path.c_str());
    std::remove(tmp_dir);
}

TEST(ConfigLoaderTest, ParseMissingFieldsSkipped) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    auto path = create_missing_fields_yaml(tmp_dir);

    auto services = parse_services(path);
    // incomplete_entry missing path → filtered out
    EXPECT_TRUE(services.empty());

    std::remove(path.c_str());
    std::remove(tmp_dir);
}

TEST(ConfigLoaderTest, ParseEmptyServicesReturnsEmpty) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    auto path = std::string(tmp_dir) + "/empty_services.yaml";
    { std::ofstream f(path); f << "services:\n"; }

    auto services = parse_services(path);
    EXPECT_TRUE(services.empty());

    std::remove(path.c_str());
    std::remove(tmp_dir);
}

// ============================================================================
// parse_modes
// ============================================================================

TEST(ConfigLoaderTest, ParseValidModes) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    auto path = create_valid_modes_yaml(tmp_dir);

    auto [modes, default_mode] = parse_modes(path);
    ASSERT_EQ(modes.size(), 2);
    EXPECT_EQ(default_mode, "teleop");
    EXPECT_EQ(modes[0].name, "standby");
    EXPECT_EQ(modes[0].services.size(), 1);
    EXPECT_EQ(modes[0].services[0], "motion");

    std::remove(path.c_str());
    std::remove(tmp_dir);
}

TEST(ConfigLoaderTest, ParseNonexistentModesFileThrows) {
    EXPECT_THROW(parse_modes("/tmp/nonexistent_modes_XXXX.yaml"), YAML::Exception);
}

TEST(ConfigLoaderTest, ParseMissingModesKeyReturnsEmpty) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    auto path = std::string(tmp_dir) + "/no_modes.yaml";
    { std::ofstream f(path); f << "other: data\n"; }

    auto [modes, default_mode] = parse_modes(path);
    EXPECT_TRUE(modes.empty());
    EXPECT_EQ(default_mode, "standby");

    std::remove(path.c_str());
    std::remove(tmp_dir);
}
