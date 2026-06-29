/**
 * Robot Runtime — 命令行入口
 *
 * 用法:
 *   robot list               列出所有服务及描述
 *   robot status [service]   查看服务状态
 *   robot start <service>    启动服务
 *   robot stop <service>     停止服务
 *   robot restart <service>  重启服务
 *   robot mode list          列出所有模式
 *   robot mode switch <mode> 切换模式
 *   robot up                 启动默认模式
 *   robot down               停止所有服务
 */
#include "core/runtime.h"
#include "gateway/cli/cmd_service.h"
#include "gateway/cli/cmd_mode.h"
#include "gateway/cli/cmd_monitor.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace robot_runtime;
using namespace robot_runtime::cli;

static std::string g_workspace_dir = ".";
static std::string g_config_dir    = "config";
static std::string g_log_dir       = "log";

static void print_usage() {
    printf(
        "Robot Runtime — 命令行接口\n"
        "\n"
        "用法:\n"
        "  robot [options] <command> [args]\n"
        "\n"
        "选项:\n"
        "  -c <dir>   配置目录路径 (默认: config/)\n"
        "  -w <dir>   工作目录路径 (默认: .)\n"
        "  -l <dir>   日志目录路径 (默认: log/)\n"
        "\n"
        "命令:\n"
        "  list                列出所有服务及描述\n"
        "  status [service]    查看服务状态（留空查看全部）\n"
        "  start <service>     启动服务\n"
        "  stop <service>      停止服务\n"
        "  restart <service>   重启服务\n"
        "  mode list           列出所有模式\n"
        "  mode switch <mode>  切换模式\n"
        "  up                  启动默认模式\n"
        "  down                停止所有服务\n"
        "  daemon              常驻模式（开启TCP远程管控，阻塞运行）\n"
    );
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    int opt;
    while ((opt = getopt(argc, argv, "c:w:l:")) != -1) {
        switch (opt) {
            case 'c': g_config_dir    = optarg; break;
            case 'w': g_workspace_dir = optarg; break;
            case 'l': g_log_dir       = optarg; break;
            default: break;
        }
    }

    int cmd_index = optind;
    if (cmd_index >= argc) {
        print_usage();
        return 1;
    }

    auto abs_path = [&](const std::string& p) -> std::string {
        if (p.empty() || p[0] == '/') return p;
        return fs::absolute(fs::path(g_workspace_dir) / p).string();
    };
    std::string abs_config_dir = abs_path(g_config_dir);
    std::string abs_log_dir    = abs_path(g_log_dir);

    Runtime runtime(g_workspace_dir, abs_config_dir, abs_log_dir);
    if (!runtime.init()) {
        return 1;
    }

    std::string cmd = argv[cmd_index];
    bool ok = false;

    auto arg = [&](int i) -> const char* {
        int idx = cmd_index + i;
        return (idx < argc) ? argv[idx] : nullptr;
    };

    // ---- robot daemon / serve ----
    if (cmd == "daemon" || cmd == "serve") {
        printf("starting daemon mode...\n");
        runtime.serve();
        ok = true;
    }
    else if (handle_service_command(runtime, cmd, arg(1))) {
        ok = true;
    }
    else if (handle_mode_command(runtime, cmd, arg(1), arg(2))) {
        ok = true;
    }
    else if (handle_monitor_command(runtime, cmd, arg(1))) {
        ok = true;
    }
    else {
        print_usage();
    }

    return ok ? 0 : 1;
}
