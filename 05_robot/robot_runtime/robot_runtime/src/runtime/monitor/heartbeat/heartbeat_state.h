#pragma once

#include <string>

namespace robot_runtime {

/// 服务在线状态枚举
enum class HeartbeatStatus {
    Offline,    /// 从未收到心跳或已标记离线
    Online,     /// 最近收到心跳，未超时
    Timeout,    /// 超过 timeout_sec 未收到心跳
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
