#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>

#include "runtime/process/service_manager/service_manager.h"

namespace fs = std::filesystem;
using robot_runtime::ServiceManager;

static std::string make_svc_dir(const std::string& parent,
                                const std::string& name) {
    auto svc_dir = parent + "/" + name;
    fs::create_directories(svc_dir);
    auto start_path = svc_dir + "/start.sh";
    std::ofstream f(start_path);
    f << "#!/usr/bin/env bash\n"
      << "NAME=\"" << name << "\"\n"
      << "echo \"[$NAME] started (PID=$$)\"\n"
      << "trap \"echo \\\"[$NAME] stopped\\\"; exit 0\" SIGTERM SIGINT\n"
      << "while true; do sleep 1; done\n";
    fs::permissions(start_path,
                    fs::perms::owner_all | fs::perms::group_all | fs::perms::others_read,
                    fs::perm_options::replace);
    return svc_dir;
}

struct DepEnv {
    std::string dir;
    DepEnv() {
        char tpl[] = "/tmp/stage0_dep_XXXXXX";
        if (!mkdtemp(tpl)) throw std::runtime_error("mkdtemp failed");
        dir = tpl;
        make_svc_dir(dir, "base_logger");
        make_svc_dir(dir, "sensor");
        make_svc_dir(dir, "perception");
        make_svc_dir(dir, "navigation");
        std::string y = dir + "/services.yaml";
        std::ofstream f(y);
        f << "services:\n"
          << "  base_logger:\n    path: " << dir << "/base_logger\n"
          << "    description: base\n    type: cpp_binary\n    depends: []\n    auto_restart: false\n"
          << "  sensor:\n    path: " << dir << "/sensor\n"
          << "    description: sensor\n    type: cpp_binary\n    depends:\n      - base_logger\n    auto_restart: false\n"
          << "  perception:\n    path: " << dir << "/perception\n"
          << "    description: perception\n    type: cpp_binary\n    depends:\n      - sensor\n    auto_restart: false\n"
          << "  navigation:\n    path: " << dir << "/navigation\n"
          << "    description: nav\n    type: cpp_binary\n    depends:\n      - perception\n    auto_restart: false\n";
    }
    ~DepEnv() { std::error_code ec; fs::remove_all(dir, ec); }
};

TEST(Stage0_DependencyOrder, ResolveStartOrder) {
    DepEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    auto order = sm.resolve_start_order();
    ASSERT_GE(order.size(), 4);
    auto pL = std::find(order.begin(), order.end(), "base_logger");
    auto pS = std::find(order.begin(), order.end(), "sensor");
    auto pP = std::find(order.begin(), order.end(), "perception");
    auto pN = std::find(order.begin(), order.end(), "navigation");
    EXPECT_LT(pL, pS); EXPECT_LT(pS, pP); EXPECT_LT(pP, pN);
}

TEST(Stage0_DependencyOrder, SequentialStartRespectsDependencies) {
    DepEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_TRUE(sm.start("navigation"));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_TRUE(sm.get("base_logger")->is_alive());
    EXPECT_TRUE(sm.get("sensor")->is_alive());
    EXPECT_TRUE(sm.get("perception")->is_alive());
    EXPECT_TRUE(sm.get("navigation")->is_alive());
    sm.stop_all();
}

TEST(Stage0_DependencyOrder, StopOrderIsReverseOfStart) {
    DepEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_TRUE(sm.start("navigation"));
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    EXPECT_TRUE(sm.stop("navigation"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(sm.get("navigation")->is_alive());
    EXPECT_TRUE(sm.stop("perception"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(sm.get("perception")->is_alive());
    EXPECT_TRUE(sm.get("sensor")->is_alive());
    EXPECT_TRUE(sm.stop("sensor"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(sm.get("sensor")->is_alive());
    sm.stop_all();
}
