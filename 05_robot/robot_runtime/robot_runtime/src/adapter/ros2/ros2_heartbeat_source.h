/**
 * @file ros2_heartbeat_source.h
 * @brief ROS2 心跳订阅适配器
 * @role adapter/ros2
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "runtime/monitor/heartbeat/i_heartbeat_source.h"

namespace robot_runtime {

class HeartbeatMonitor;

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
