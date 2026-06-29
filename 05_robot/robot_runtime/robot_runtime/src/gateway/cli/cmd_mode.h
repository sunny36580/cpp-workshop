#pragma once

#include "core/runtime.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace robot_runtime::cli {

inline void print_mode_list(ModeManager& mm) {
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
        printf("  %-15s [%s]%s\n", name.c_str(), svc_str.c_str(),
               is_current ? " <- current" : "");
    }
    printf("  %s\n", std::string(50, '-').c_str());
    printf("  Default: %s\n\n", mm.default_mode().c_str());
}

inline bool handle_mode_command(Runtime& runtime, const std::string& cmd,
                                 const char* arg1, const char* arg2) {
    if (cmd == "mode") {
        if (arg1 && strcmp(arg1, "list") == 0) {
            print_mode_list(runtime.mode_manager());
            return true;
        }
        if (arg1 && strcmp(arg1, "switch") == 0 && arg2) {
            bool ok = runtime.switch_mode(arg2);
            return ok;
        }
    }
    return false;
}

} // namespace robot_runtime::cli
