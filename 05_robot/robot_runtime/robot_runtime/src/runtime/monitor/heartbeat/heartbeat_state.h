/**
 * @file heartbeat_state.h
 * @brief 心跳状态类型
 * @role runtime/monitor/heartbeat
 */
#pragma once

#include <string>

namespace robot_runtime {

/**
 * @brief 服务在线状态
 */
enum class HeartbeatStatus {
    Offline,
    Online,
    Timeout,
};

inline const char* to_string(HeartbeatStatus s) {
    switch (s) {
        case HeartbeatStatus::Offline:  return "Offline";
        case HeartbeatStatus::Online:   return "Online";
        case HeartbeatStatus::Timeout:  return "Timeout";
    }
    return "Unknown";
}

/// 单个服务当前心跳状态快照
struct HeartbeatState {
    std::string service_name;
    HeartbeatStatus status = HeartbeatStatus::Offline;
    double last_heartbeat_time = 0.0;
    double timeout_sec = 8.0;
};

} // namespace robot_runtime
