#pragma once

#include <string>

namespace robot_runtime {

/// 被监控服务的配置
struct HeartbeatTarget {
    std::string name;
    double timeout_sec = 8.0;
};

} // namespace robot_runtime
