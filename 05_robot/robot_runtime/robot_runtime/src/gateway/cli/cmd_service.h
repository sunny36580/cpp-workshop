#pragma once

#include "core/runtime.h"

#include <cstdio>
#include <string>

namespace robot_runtime::cli {

inline void print_status(ServiceManager& sm) {
    auto statuses = sm.all_status();
    printf("\n");
    printf("  %-20s %-12s %-8s %-8s %-10s Depends\n", "Service", "State", "PID", "Alive", "Type");
    printf("  %s\n", std::string(80, '-').c_str());
    for (const auto& s : statuses) {
        const char* alive_mark = s.alive ? "✓" : "✗";
        std::string pid_str = s.pid > 0 ? std::to_string(s.pid) : "-";
        std::string deps;
        for (size_t i = 0; i < s.depends.size(); ++i) {
            if (i > 0) deps += ", ";
            deps += s.depends[i];
        }
        if (deps.empty()) deps = "-";
        printf("  %-20s %-12s %-8s %-8s %-10s %s\n",
               s.name.c_str(), to_string(s.state),
               pid_str.c_str(), alive_mark,
               s.type.c_str(), deps.c_str());
    }
    printf("\n");
}

inline void print_service_detail(const ServiceStatus& s) {
    printf("\n");
    printf("  %-16s %s\n",  "Service:",   s.name.c_str());
    printf("  %-16s %s\n",  "Description:", s.description.empty() ? "-" : s.description.c_str());
    printf("  %-16s %s\n",  "Type:",      s.type.c_str());
    printf("  %-16s %s\n",  "State:",     to_string(s.state));
    printf("  %-16s %d\n",  "PID:",       s.pid);
    printf("  %-16s %s\n",  "Alive:",     s.alive ? "✓" : "✗");
    std::string deps;
    for (size_t i = 0; i < s.depends.size(); ++i) {
        if (i > 0) deps += ", ";
        deps += s.depends[i];
    }
    printf("  %-16s %s\n",  "Depends:",   deps.empty() ? "-" : deps.c_str());
    printf("\n");
}

inline void print_service_list(ServiceManager& sm) {
    auto statuses = sm.all_status();
    printf("\n");
    printf("  %-20s %-12s %-10s %s\n", "Service", "Type", "State", "Description");
    printf("  %s\n", std::string(80, '-').c_str());
    for (const auto& s : statuses) {
        printf("  %-20s %-12s %-10s %s\n",
               s.name.c_str(), s.type.c_str(),
               to_string(s.state),
               s.description.empty() ? "-" : s.description.c_str());
    }
    printf("\n  %zu service(s) registered\n\n", statuses.size());
}

inline bool handle_service_command(Runtime& runtime, const std::string& cmd,
                                    const char* arg1) {
    if (cmd == "list") {
        print_service_list(runtime.service_manager());
        return true;
    }
    if (cmd == "status") {
        if (arg1) {
            auto svc = runtime.get_service(arg1);
            if (svc) { print_service_detail(svc->status()); return true; }
            fprintf(stderr, "Unknown service: %s\n", arg1);
            return false;
        }
        print_status(runtime.service_manager());
        return true;
    }
    if (cmd == "start" && arg1) {
        bool ok = runtime.start_service(arg1);
        print_status(runtime.service_manager());
        return ok;
    }
    if (cmd == "stop" && arg1) {
        bool ok = runtime.stop_service(arg1);
        print_status(runtime.service_manager());
        return ok;
    }
    if (cmd == "restart" && arg1) {
        bool ok = runtime.restart_service(arg1);
        print_status(runtime.service_manager());
        return ok;
    }
    if (cmd == "up") {
        printf("starting default mode...\n");
        runtime.apply_default_mode();
        print_status(runtime.service_manager());
        return true;
    }
    if (cmd == "down") {
        printf("stopping all services...\n");
        runtime.stop_all();
        print_status(runtime.service_manager());
        return true;
    }
    return false;
}

} // namespace robot_runtime::cli
