/**
 * @file heartbeat_target.h
 * @brief 心跳监控目标
 * @role runtime/monitor/heartbeat
 */
#pragma once

#include <string>

namespace robot_runtime {

/**
 * @brief 被监控服务的配置
 */
struct HeartbeatTarget {
    std::string name;
    double timeout_sec = 8.0;
};

} // namespace robot_runtime
