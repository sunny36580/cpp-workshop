#pragma once

#include "core/runtime.h"
#include "runtime/monitor/monitor_manager.h"

#include <cstdio>
#include <string>

namespace robot_runtime::cli {

inline bool handle_monitor_command(Runtime&, const std::string&, const char*) {
    return false;  // 预留
}

} // namespace robot_runtime::cli
