/**
 * Robot Runtime — 命令行入口
 *
 * 用法：
 *   robot start <service>     启动服务
 *   robot stop <service>      停止服务
 *   robot restart <service>   重启服务
 *   robot status              查看所有服务状态
 *   robot mode list           列出所有模式
 *   robot mode switch <mode>  切换模式
 *   robot up                  启动默认模式
 *   robot down                停止所有服务
 */
#include "runtime/core/service_manager.h"
#include "runtime/managers/mode_manager.h"
#include "runtime/managers/monitor_manager.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;

// ============================================================================
// 默认路径（可通过 -c 覆盖）
// ============================================================================
static std::string g_workspace_dir;
static std::string g_config_dir;
static std::string g_log_dir;

// ============================================================================
// 格式化输出
// ============================================================================
static void print_status(robot_runtime::ServiceManager& sm) {
    auto statuses = sm.all_status();

    printf("\n");
    printf("  %-20s %-12s %-8s %-8s Depends\n", "Service", "State", "PID", "Alive");
    printf("  %s\n", std::string(70, '-').c_str());

    for (const auto& s : statuses) {
        const char* alive_mark = s.alive ? "✓" : "✗";
        std::string pid_str = s.pid > 0 ? std::to_string(s.pid) : "-";
        std::string deps;
        for (size_t i = 0; i < s.depends.size(); ++i) {
            if (i > 0) deps += ", ";
            deps += s.depends[i];
        }
        if (deps.empty()) deps = "-";

        printf("  %-20s %-12s %-8s %-8s %s\n",
               s.name.c_str(), to_string(s.state),
               pid_str.c_str(), alive_mark, deps.c_str());
    }
    printf("\n");
}

static void print_mode_list(robot_runtime::ModeManager& mm) {
    printf("\n");
    printf("  Available modes (%zu):\n", mm.modes().size());
    printf("  %s\n", std::string(50, '-').c_str());

    for (const auto& [name, services] : mm.modes()) {
        std::string svc_str;
        for (size_t i = 0; i < services.size(); ++i) {
            if (i > 0) svc_str += ", ";
            svc_str += services[i];
        }
        bool is_current = (name == mm.current_mode());
        printf("  %-15s [%s]%s\n",
               name.c_str(), svc_str.c_str(),
               is_current ? " <- current" : "");
    }

    printf("  %s\n", std::string(50, '-').c_str());
    printf("  Default: %s\n\n", mm.default_mode().c_str());
}

static void print_usage() {
    printf(
        "Robot Runtime — 命令行接口\n"
        "\n"
        "用法:\n"
        "  robot [options] <command> [args]\n"
        "\n"
        "选项:\n"
        "  -c <dir>   配置目录路径 (默认: configs/)\n"
        "  -w <dir>   工作目录路径 (默认: .)\n"
        "  -l <dir>   日志目录路径 (默认: logs/)\n"
        "\n"
        "命令:\n"
        "  start <service>     启动服务\n"
        "  stop <service>      停止服务\n"
        "  restart <service>   重启服务\n"
        "  status              查看所有服务状态\n"
        "  mode list           列出所有模式\n"
        "  mode switch <mode>  切换模式\n"
        "  up                  启动默认模式\n"
        "  down                停止所有服务\n"
    );
}

// ============================================================================
// 主入口
// ============================================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    // 解析全局选项（放在子命令之前）
    // 用法: robot -c /path/to/config <command> ...
    g_config_dir = "configs";
    g_log_dir    = "logs";
    g_workspace_dir = ".";

    int opt;
    while ((opt = getopt(argc, argv, "c:w:l:")) != -1) {
        switch (opt) {
            case 'c': g_config_dir = optarg; break;
            case 'w': g_workspace_dir = optarg; break;
            case 'l': g_log_dir = optarg; break;
            default: break;
        }
    }

    // optind 指向第一个非选项参数（即子命令）
    int cmd_index = optind;
    if (cmd_index >= argc) {
        print_usage();
        return 1;
    }

    // 解析为绝对路径（相对路径相对于工作目录）
    auto abs_path = [&](const std::string& p) -> std::string {
        if (p.empty() || p[0] == '/') return p;
        return fs::absolute(fs::path(g_workspace_dir) / p).string();
    };
    std::string abs_config_dir = abs_path(g_config_dir);
    std::string abs_log_dir    = abs_path(g_log_dir);

    // 确保目录存在
    fs::create_directories(abs_log_dir);
    fs::create_directories(abs_log_dir + "/services");

    // 初始化核心组件
    // 从 configs/services.yaml 统一加载服务配置
    // 格式：services: { name: { path, depends, auto_restart } }
    robot_runtime::ServiceManager sm(g_workspace_dir, abs_config_dir, abs_log_dir);
    sm.load_config("services.yaml");

    robot_runtime::ModeManager mm(abs_config_dir, &sm);
    mm.load_config("modes.yaml");

    robot_runtime::MonitorManager mon(&sm);
    mon.start();

    std::string cmd = argv[cmd_index];
    bool ok = false;

    auto arg = [&](int i) -> const char* {
        int idx = cmd_index + i;
        return (idx < argc) ? argv[idx] : nullptr;
    };

    // ---- robot start <service> ----
    if (cmd == "start" && arg(1)) {
        ok = sm.start(arg(1));
        print_status(sm);
    }
    // ---- robot stop <service> ----
    else if (cmd == "stop" && arg(1)) {
        ok = sm.stop(arg(1));
        print_status(sm);
    }
    // ---- robot restart <service> ----
    else if (cmd == "restart" && arg(1)) {
        ok = sm.restart(arg(1));
        print_status(sm);
    }
    // ---- robot status ----
    else if (cmd == "status") {
        print_status(sm);
        ok = true;
    }
    // ---- robot mode list ----
    else if (cmd == "mode" && arg(1) && strcmp(arg(1), "list") == 0) {
        print_mode_list(mm);
        ok = true;
    }
    // ---- robot mode switch <mode> ----
    else if (cmd == "mode" && arg(1) && strcmp(arg(1), "switch") == 0 && arg(2)) {
        ok = mm.switch_to(arg(2));
        print_status(sm);
    }
    // ---- robot up ----
    else if (cmd == "up") {
        printf("starting default mode...\n");
        mm.apply_default();
        print_status(sm);
        ok = true;
    }
    // ---- robot down ----
    else if (cmd == "down") {
        printf("stopping all services...\n");
        sm.stop_all();
        print_status(sm);
        ok = true;
    }
    else {
        print_usage();
    }

    mon.stop();
    return ok ? 0 : 1;
}
