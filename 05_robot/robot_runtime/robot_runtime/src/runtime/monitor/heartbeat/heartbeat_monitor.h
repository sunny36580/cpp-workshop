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

/// 心跳状态变化回调
using HeartbeatStatusCallback = std::function<void(const HeartbeatState& state)>;

/// 心跳监控器 —— 纯状态机，无 I/O 无 ROS 依赖
///
/// 职责：
///   1. 注册被监控服务（AddTarget）
///   2. 接收心跳事件更新状态（OnHeartbeat）
///   3. 定时检查超时（Start 启动后台线程）
///   4. 状态变化时触发回调
///
/// 不负责：
///   - 订阅 ROS2 话题
///   - 写文件
///   - UDP 上报
///   - 服务重启决策
class HeartbeatMonitor {
public:
    HeartbeatMonitor() = default;
    ~HeartbeatMonitor();

    // ---- 配置 ----

    /// 添加被监控服务
    void AddTarget(const HeartbeatTarget& target);

    /// 批量添加
    void AddTargets(const std::vector<HeartbeatTarget>& targets);

    /// 设置全局超时阈值（秒，对未指定 timeout 的 target 生效）
    void SetDefaultTimeoutSec(double sec) { default_timeout_sec_ = sec; }

    // ---- 生命周期 ----

    /// 启动后台超时检测线程
    void Start();

    /// 停止后台线程
    void Stop();

    // ---- 事件输入 ----

    /// 收到一条心跳事件（由适配器调用）
    void OnHeartbeat(const HeartbeatEvent& event);

    // ---- 状态查询 ----

    /// 获取指定服务状态
    HeartbeatState GetState(const std::string& service_name) const;

    /// 获取所有服务状态
    std::vector<HeartbeatState> GetAllStates() const;

    /// 获取当前所有超时服务
    std::vector<HeartbeatState> GetTimeoutServices() const;

    // ---- 回调 ----

    /// 设置状态变化回调（Online↔Timeout 切换时触发）
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

    // 配置
    double default_timeout_sec_ = 8.0;

    // 状态表
    std::map<std::string, InnerState> states_;
    mutable std::mutex mutex_;

    // 后台线程
    std::atomic<bool> running_{false};
    std::thread check_thread_;

    // 回调
    HeartbeatStatusCallback status_cb_;
};

} // namespace robot_runtime
