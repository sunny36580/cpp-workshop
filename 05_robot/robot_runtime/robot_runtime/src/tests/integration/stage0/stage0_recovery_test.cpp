#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <signal.h>
#include <sys/wait.h>

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

struct RecEnv {
    std::string dir;
    RecEnv() {
        char tpl[] = "/tmp/s0_rec_XXXXXX";
        if (!mkdtemp(tpl)) throw std::runtime_error("mkdtemp failed");
        dir = tpl;

        make_svc(dir, "ok_svc",
            "NAME=\"ok_svc\"\n"
            "echo \"[$NAME] started\"\n"
            "trap \"exit 0\" SIGTERM SIGINT\n"
            "while true; do sleep 1; done\n");

        make_svc(dir, "crash_svc",
            "echo \"crashed\"\nexit 1\n");

        std::string y;
        y += "services:\n";
        y += "  ok_svc:\n    path: " + dir + "/ok_svc\n    description: ok\n    type: cpp_binary\n    depends: []\n    auto_restart: true\n";
        y += "  crash_svc:\n    path: " + dir + "/crash_svc\n    description: crash\n    type: cpp_binary\n    depends: []\n    auto_restart: true\n";
        std::ofstream f(dir + "/services.yaml"); f << y;
    }
    ~RecEnv() { std::error_code ec; fs::remove_all(dir, ec); }
};

TEST(Stage0_Recovery, KillServiceDetected) {
    RecEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    ASSERT_TRUE(sm.start("ok_svc"));
    auto svc = sm.get("ok_svc");
    ASSERT_NE(svc, nullptr);
    pid_t pid = svc->pid();
    ASSERT_GT(pid, 0);
    kill(pid, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int wstatus;
    waitpid(pid, &wstatus, WNOHANG);
    EXPECT_FALSE(svc->is_alive());
    sm.stop_all();
}

TEST(Stage0_Recovery, CrashService) {
    RecEnv env;
    ServiceManager sm(".", env.dir, "/tmp");
    ASSERT_TRUE(sm.load_config("services.yaml"));
    EXPECT_TRUE(sm.start("crash_svc"));
    auto svc = sm.get("crash_svc");
    ASSERT_NE(svc, nullptr);
    EXPECT_GT(svc->pid(), 0);
}
