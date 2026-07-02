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

static void make_svc(const std::string& parent, const std::string& name,
                     const std::string& body) {
    auto d = parent + "/" + name;
    fs::create_directories(d);
    auto sp = d + "/start.sh";
    std::ofstream f(sp);
    f << "#!/usr/bin/env bash\n" << body;
    fs::permissions(sp, fs::perms::owner_all | fs::perms::group_all | fs::perms::others_read,
                    fs::perm_options::replace);
}

static void write_yaml(const std::string& path, const std::string& content) {
    std::ofstream f(path); f << content;
}

struct FakeEnv {
    std::string dir;
    FakeEnv() {
        char tpl[] = "/tmp/s0_life_XXXXXX";
        if (!mkdtemp(tpl)) throw std::runtime_error("mkdtemp failed");
        dir = tpl;

        make_svc(dir, "ok_svc",
            "NAME=\"ok_svc\"\n"
            "echo \"[$NAME] started (PID=$$)\"\n"
            "trap \"echo \\\"[$NAME] stopped\\\"; exit 0\" SIGTERM SIGINT\n"
            "while true; do sleep 1; done\n");

        make_svc(dir, "crash_svc",
            "echo \"crashed\"\nexit 1\n");

        std::string y;
        y += "services:\n";
        y += "  ok_svc:\n    path: " + dir + "/ok_svc\n    description: ok\n    type: cpp_binary\n    depends: []\n    auto_restart: false\n";
        y += "  crash_svc:\n    path: " + dir + "/crash_svc\n    description: crash\n    type: cpp_binary\n    depends: []\n    auto_restart: false\n";
        write_yaml(dir + "/services.yaml", y);
    }
    ~FakeEnv() { std::error_code ec; fs::remove_all(dir, ec); }
};

TEST(Stage0_ServiceLifecycle, StartAndStopOkService) {
    FakeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_TRUE(sm.start("ok_svc"));
    auto svc = sm.get("ok_svc");
    ASSERT_NE(svc, nullptr);
    EXPECT_TRUE(svc->is_alive());
    EXPECT_TRUE(sm.stop("ok_svc"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_FALSE(svc->is_alive());
}

TEST(Stage0_ServiceLifecycle, CrashService) {
    FakeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_TRUE(sm.start("crash_svc"));
    auto svc = sm.get("crash_svc");
    ASSERT_NE(svc, nullptr);
    EXPECT_GT(svc->pid(), 0);
}

TEST(Stage0_ServiceLifecycle, DoubleStartIsIdempotent) {
    FakeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_TRUE(sm.start("ok_svc"));
    auto pid1 = sm.get("ok_svc")->pid();
    EXPECT_TRUE(sm.start("ok_svc"));  // 再次启动，应无副作用
    auto pid2 = sm.get("ok_svc")->pid();
    EXPECT_EQ(pid1, pid2);
    sm.stop("ok_svc");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}

TEST(Stage0_ServiceLifecycle, StopNonExistent) {
    FakeEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_FALSE(sm.start("nonexistent"));
    EXPECT_FALSE(sm.stop("nonexistent"));
    EXPECT_EQ(sm.get("nonexistent"), nullptr);
}
