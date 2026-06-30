#pragma once

#include <string>
#include <vector>

namespace robot_runtime {

/// 心跳事件源抽象接口
///
/// 所有心跳适配器（ROS2 / TCP / 自定义协议）实现此接口，
/// 将外部心跳消息转换为 HeartbeatEvent 送入 HeartbeatMonitor。
///
/// Runtime 核心只依赖此接口，不依赖具体适配器实现。
class IHeartbeatSource {
public:
    virtual ~IHeartbeatSource() = default;

    /// 启动订阅/监听
    virtual bool Start(const std::vector<std::string>& config) = 0;

    /// 停止
    virtual void Stop() = 0;

    /// 是否正在运行
    virtual bool running() const = 0;
};

} // namespace robot_runtime
