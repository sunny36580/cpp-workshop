/**
 * @file i_heartbeat_source.h
 * @brief 心跳事件源抽象接口
 * @role runtime/monitor/heartbeat
 */
#pragma once

#include <string>
#include <vector>

namespace robot_runtime {

/**
 * @class IHeartbeatSource
 * @brief 心跳事件源抽象接口
 * @responsibility 将外部心跳消息转换为 HeartbeatEvent
 */
class IHeartbeatSource {
public:
    virtual ~IHeartbeatSource() = default;

    /**
     * @brief 启动订阅
     * @param config 配置参数列表
     * @return true=启动成功
     */
    virtual bool Start(const std::vector<std::string>& config) = 0;

    /**
     * @brief 停止
     */
    virtual void Stop() = 0;

    /**
     * @brief 是否正在运行
     * @return true=运行中
     */
    virtual bool running() const = 0;
};

} // namespace robot_runtime
