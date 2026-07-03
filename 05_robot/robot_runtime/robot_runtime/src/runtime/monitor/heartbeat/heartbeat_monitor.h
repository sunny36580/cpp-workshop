/**
 * @file heartbeat_monitor.h
 * @brief 纯内存心跳状态机
 * @role runtime/monitor/heartbeat
 */
#pragma once

#include "runtime/monitor/heartbeat/heartbeat_event.h"
#include "runtime/monitor/heartbeat/heartbeat_target.h"
#include "runtime/monitor/heartbeat/heartbeat_state.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <atomic>
#include <thread>
#include <functional>
#include <mutex>

namespace robot_runtime {

using HeartbeatStatusCallback = std::function<void(const HeartbeatState& state)>;

/**
 * @class HeartbeatMonitor
 * @brief 心跳状态机，无 IO 无 ROS 依赖
 * @responsibility 接收心跳事件、超时检测、状态变化回调
 */
class HeartbeatMonitor {
public:
    HeartbeatMonitor() = default;
    ~HeartbeatMonitor();

    /**
     * @brief 注册被监控服务
     * @param target 监控目标
     */
    void AddTarget(const HeartbeatTarget& target);
    /**
     * @brief 批量注册监控目标
     * @param targets 监控目标列表
     */
    void AddTargets(const std::vector<HeartbeatTarget>& targets);
    /**
     * @brief 设置全局默认超时阈值
     * @param sec 超时秒数
     */
    void SetDefaultTimeoutSec(double sec) { default_timeout_sec_ = sec; }

    /**
     * @brief 启动后台超时检测线程
     */
    void Start();
    /**
     * @brief 停止后台线程
     */
    void Stop();

    /**
     * @brief 收到一条心跳事件
     * @param event 心跳事件
     */
    void OnHeartbeat(const HeartbeatEvent& event);

    /**
     * @brief 获取指定服务心跳状态
     * @param service_name 服务名
     * @return 心跳状态快照
     */
    HeartbeatState GetState(const std::string& service_name) const;
    /**
     * @brief 获取所有服务心跳状态
     * @return 全部状态列表
     */
    std::vector<HeartbeatState> GetAllStates() const;
    /**
     * @brief 获取超时服务列表
     * @return 超时服务列表
     */
    std::vector<HeartbeatState> GetTimeoutServices() const;

    /**
     * @brief 设置状态变化回调
     * @param cb Online/Timeout 切换时触发
     */
    void SetStatusCallback(HeartbeatStatusCallback cb) { status_cb_ = std::move(cb); }

private:
    struct InnerState {
        HeartbeatStatus status = HeartbeatStatus::Offline;
        double last_time = 0.0;
        double timeout_sec = 8.0;
        HeartbeatStatus prev_status = HeartbeatStatus::Offline;  // 上次回调时的状态
    };

    void CheckLoop();
    void SetStatus(const std::string& name, HeartbeatStatus new_status,
                   InnerState& st, double now);

    double default_timeout_sec_ = 8.0;    // 默认超时阈值(秒)
    std::map<std::string, InnerState> states_;  // 服务名→心跳状态
    mutable std::mutex mutex_;
    std::atomic<bool> running_{false};
    std::thread check_thread_;

    // 回调
    HeartbeatStatusCallback status_cb_;
};

} // namespace robot_runtime
