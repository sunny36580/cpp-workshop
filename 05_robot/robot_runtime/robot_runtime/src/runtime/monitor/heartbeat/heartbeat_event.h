/**
 * @file heartbeat_event.h
 * @brief 心跳事件定义
 * @role runtime/monitor/heartbeat
 */
#pragma once

#include <string>

namespace robot_runtime {

/**
 * @brief 单次心跳事件（适配器→HeartbeatMonitor）
 */
struct HeartbeatEvent {
    std::string service_name;
    double timestamp = 0.0;  // epoch seconds
};

} // namespace robot_runtime
