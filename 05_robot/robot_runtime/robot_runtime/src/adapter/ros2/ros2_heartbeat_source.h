#pragma once

#include <memory>
#include <string>
#include <vector>

#include "runtime/monitor/heartbeat/i_heartbeat_source.h"

namespace robot_runtime {

class HeartbeatMonitor;

/// ROS2 心跳事件源
///
/// 实现 IHeartbeatSource 接口，订阅 ROS2 心跳话题，
/// 将收到的消息转换为 HeartbeatEvent 送入 HeartbeatMonitor。
///
/// 编译条件：
///   - HAS_ROS2=1 时：链接 rclcpp，实际订阅话题
///   - 否则：空实现，仅日志提示
class Ros2HeartbeatSource : public IHeartbeatSource {
public:
    explicit Ros2HeartbeatSource(HeartbeatMonitor* monitor);
    ~Ros2HeartbeatSource() override;

    bool Start(const std::vector<std::string>& topics) override;
    void Stop() override;
    bool running() const override { return running_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    HeartbeatMonitor* monitor_ = nullptr;
    bool running_ = false;
};

} // namespace robot_runtime
