#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>

#include "runtime/process/service_manager/service_manager.h"
#include "orchestration/mode/mode_manager.h"

namespace fs = std::filesystem;
using robot_runtime::ServiceManager;
using robot_runtime::ModeManager;
using robot_runtime::ServiceState;

static void make_svc(const std::string& parent, const std::string& name) {
    auto d = parent + "/" + name;
    fs::create_directories(d);
    auto sp = d + "/start.sh";
    std::ofstream f(sp);
    f << "#!/usr/bin/env bash\n"
      << "NAME=\"" << name << "\"\n"
      << "echo \"[$NAME] started (PID=$$)\"\n"
      << "trap \"echo \\\"[$NAME] stopped\\\"; exit 0\" SIGTERM SIGINT\n"
      << "while true; do sleep 1; done\n";
    fs::permissions(sp, fs::perms::owner_all | fs::perms::group_all | fs::perms::others_read,
                    fs::perm_options::replace);
}

struct ModeEnv {
    std::string dir;
    ModeEnv() {
        char tpl[] = "/tmp/s0_mode_XXXXXX";
        if (!mkdtemp(tpl)) throw std::runtime_error("mkdtemp failed");
        dir = tpl;

        for (auto n : {"base_logger", "sensor", "voice", "perception", "navigation"})
            make_svc(dir, n);

        std::string s;
        s += "services:\n";
        s += "  base_logger:\n    path: " + dir + "/base_logger\n    description: base\n    type: cpp_binary\n    depends: []\n    auto_restart: false\n";
        s += "  sensor:\n    path: " + dir + "/sensor\n    description: sensor\n    type: cpp_binary\n    depends:\n      - base_logger\n    auto_restart: false\n";
        s += "  voice:\n    path: " + dir + "/voice\n    description: voice\n    type: cpp_binary\n    depends:\n      - base_logger\n    auto_restart: false\n";
        s += "  perception:\n    path: " + dir + "/perception\n    description: perception\n    type: cpp_binary\n    depends:\n      - sensor\n    auto_restart: false\n";
        s += "  navigation:\n    path: " + dir + "/navigation\n    description: nav\n    type: cpp_binary\n    depends:\n      - perception\n    auto_restart: false\n";
        std::ofstream f(dir + "/services.yaml"); f << s;

        std::string m;
        m += "modes:\n";
        m += "  idle:\n    services:\n      - base_logger\n";
        m += "  manual:\n    services:\n      - base_logger\n      - sensor\n      - voice\n";
        m += "  auto:\n    services:\n      - base_logger\n      - sensor\n      - perception\n      - navigation\n";
        m += "  default: idle\n";
        std::ofstream g(dir + "/modes.yaml"); g << m;
    }
    ~ModeEnv() { std::error_code ec; fs::remove_all(dir, ec); }
};

TEST(Stage0_ModeSwitch, IdleModeOnlyBaseLogger) {
    ModeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    ModeManager mm(env.dir, &sm);
    ASSERT_TRUE(mm.load_config("modes.yaml"));
    mm.apply_default();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_TRUE(sm.get("base_logger")->is_alive());
    EXPECT_EQ(sm.get("sensor")->state(), ServiceState::STOPPED);
    EXPECT_EQ(sm.get("voice")->state(), ServiceState::STOPPED);
    EXPECT_EQ(sm.get("perception")->state(), ServiceState::STOPPED);
    sm.stop_all();
}

TEST(Stage0_ModeSwitch, ManualModeStartsCorrectServices) {
    ModeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    ModeManager mm(env.dir, &sm);
    ASSERT_TRUE(mm.load_config("modes.yaml"));
    mm.switch_to("manual");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_TRUE(sm.get("base_logger")->is_alive());
    EXPECT_TRUE(sm.get("sensor")->is_alive());
    EXPECT_TRUE(sm.get("voice")->is_alive());
    EXPECT_EQ(sm.get("perception")->state(), ServiceState::STOPPED);
    sm.stop_all();
}

TEST(Stage0_ModeSwitch, AutoModeCorrectServiceSet) {
    ModeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    ModeManager mm(env.dir, &sm);
    ASSERT_TRUE(mm.load_config("modes.yaml"));
    mm.switch_to("manual");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_TRUE(sm.get("voice")->is_alive());
    mm.switch_to("auto");
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    EXPECT_TRUE(sm.get("base_logger")->is_alive());
    EXPECT_TRUE(sm.get("sensor")->is_alive());
    EXPECT_TRUE(sm.get("perception")->is_alive());
    EXPECT_TRUE(sm.get("navigation")->is_alive());
    EXPECT_EQ(sm.get("voice")->state(), ServiceState::STOPPED);
    sm.stop_all();
}

TEST(Stage0_ModeSwitch, UnknownModeReturnsFalse) {
    ModeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    ModeManager mm(env.dir, &sm);
    ASSERT_TRUE(mm.load_config("modes.yaml"));
    EXPECT_FALSE(mm.switch_to("nonexistent_mode"));
}
