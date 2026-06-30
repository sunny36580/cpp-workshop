#pragma once

#include <string>

namespace robot_runtime {

/// 单次心跳事件（由适配器生成，送入 HeartbeatMonitor）
struct HeartbeatEvent {
    std::string service_name;
    double timestamp = 0.0;  // epoch seconds
};

} // namespace robot_runtime
