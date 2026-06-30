#include <gtest/gtest.h>

#include "orchestration/mode/mode_manager.h"
#include "runtime/process/service_manager/service_manager.h"

#include <cstdio>
#include <fstream>

using robot_runtime::ModeManager;
using robot_runtime::ServiceManager;

// ============================================================================
// 辅助：创建临时 modes.yaml 配置文件
// ============================================================================
static std::string create_modes_yaml(const std::string& dir) {
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
  default: standby
)";
    return path;
}

// ============================================================================
// ModeManager
// ============================================================================

TEST(ModeManagerTest, LoadConfigLoadsModes) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    create_modes_yaml(tmp_dir);

    ServiceManager sm(".", tmp_dir, "/tmp");
    ModeManager mm(tmp_dir, &sm);

    EXPECT_TRUE(mm.load_config("modes.yaml"));
    EXPECT_EQ(mm.default_mode(), "standby");
    EXPECT_EQ(mm.modes().size(), 2);  // standby, teleop

    // cleanup
    std::remove((std::string(tmp_dir) + "/modes.yaml").c_str());
    std::remove(tmp_dir);
}

TEST(ModeManagerTest, DefaultModeIsStandby) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    create_modes_yaml(tmp_dir);

    ServiceManager sm(".", tmp_dir, "/tmp");
    ModeManager mm(tmp_dir, &sm);
    mm.load_config("modes.yaml");

    EXPECT_EQ(mm.default_mode(), "standby");

    std::remove((std::string(tmp_dir) + "/modes.yaml").c_str());
    std::remove(tmp_dir);
}

TEST(ModeManagerTest, SwitchToUnknownModeReturnsFalse) {
    char tmp_dir[] = "/tmp/robot_test_XXXXXX";
    ASSERT_NE(mkdtemp(tmp_dir), nullptr);
    create_modes_yaml(tmp_dir);

    ServiceManager sm(".", tmp_dir, "/tmp");
    ModeManager mm(tmp_dir, &sm);
    mm.load_config("modes.yaml");

    EXPECT_FALSE(mm.switch_to("nonexistent"));

    std::remove((std::string(tmp_dir) + "/modes.yaml").c_str());
    std::remove(tmp_dir);
}

TEST(ModeManagerTest, LoadNonexistentConfigReturnsFalse) {
    ServiceManager sm(".", "/tmp", "/tmp");
    ModeManager mm("/tmp", &sm);
    EXPECT_FALSE(mm.load_config("nonexistent_file.yaml"));
}
