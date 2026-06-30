#include "runtime/monitor/heartbeat/heartbeat_monitor.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <ctime>
#include <algorithm>

namespace robot_runtime {

// =====================================================================
// 析构
// =====================================================================
HeartbeatMonitor::~HeartbeatMonitor() { Stop(); }

// =====================================================================
// 配置
// =====================================================================
void HeartbeatMonitor::AddTarget(const HeartbeatTarget& target)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto& st = states_[target.name];
    st.timeout_sec = target.timeout_sec > 0 ? target.timeout_sec : default_timeout_sec_;
    st.status = HeartbeatStatus::Offline;
    st.prev_status = HeartbeatStatus::Offline;
    printf("[HeartbeatMonitor] 注册服务: %s (timeout=%.1fs)\n",
           target.name.c_str(), st.timeout_sec);
}

void HeartbeatMonitor::AddTargets(const std::vector<HeartbeatTarget>& targets)
{
    for (const auto& t : targets) AddTarget(t);
}

// =====================================================================
// 生命周期
// =====================================================================
void HeartbeatMonitor::Start()
{
    if (running_.exchange(true)) return;
    check_thread_ = std::thread(&HeartbeatMonitor::CheckLoop, this);
    printf("[HeartbeatMonitor] started\n");
}

void HeartbeatMonitor::Stop()
{
    if (!running_.exchange(false)) return;
    if (check_thread_.joinable()) check_thread_.join();
    printf("[HeartbeatMonitor] stopped\n");
}

// =====================================================================
// 事件输入
// =====================================================================
void HeartbeatMonitor::OnHeartbeat(const HeartbeatEvent& event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(event.service_name);
    if (it == states_.end()) return;  // 未注册的服务忽略

    auto& st = it->second;
    st.last_time = event.timestamp;

    if (st.status != HeartbeatStatus::Online) {
        // 从 Offline/Timeout → Online
        SetStatus(it->first, HeartbeatStatus::Online, st, event.timestamp);
    }
}

// =====================================================================
// 状态查询
// =====================================================================
HeartbeatState HeartbeatMonitor::GetState(const std::string& service_name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = states_.find(service_name);
    if (it == states_.end()) {
        return {service_name, HeartbeatStatus::Offline, 0.0, default_timeout_sec_};
    }
    return {it->first, it->second.status, it->second.last_time, it->second.timeout_sec};
}

std::vector<HeartbeatState> HeartbeatMonitor::GetAllStates() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<HeartbeatState> result;
    for (const auto& [name, st] : states_) {
        result.push_back({name, st.status, st.last_time, st.timeout_sec});
    }
    return result;
}

std::vector<HeartbeatState> HeartbeatMonitor::GetTimeoutServices() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<HeartbeatState> result;
    for (const auto& [name, st] : states_) {
        if (st.status == HeartbeatStatus::Timeout) {
            result.push_back({name, st.status, st.last_time, st.timeout_sec});
        }
    }
    return result;
}

// =====================================================================
// 内部
// =====================================================================
void HeartbeatMonitor::SetStatus(const std::string& name, HeartbeatStatus new_status,
                                  InnerState& st, double now)
{
    if (st.status == new_status) return;
    st.status = new_status;

    // 状态变化时才触发回调
    if (status_cb_) {
        HeartbeatState s{name, new_status, st.last_time, st.timeout_sec};
        status_cb_(s);
    }
}

void HeartbeatMonitor::CheckLoop()
{
    while (running_) {
        double now = 0.0;
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            now = static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
        }

        std::vector<std::pair<std::string, HeartbeatStatus>> changes;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& [name, st] : states_) {
                if (st.status == HeartbeatStatus::Offline) continue;

                bool timed_out = (now - st.last_time) > st.timeout_sec;
                if (timed_out && st.status != HeartbeatStatus::Timeout) {
                    st.status = HeartbeatStatus::Timeout;
                    changes.emplace_back(name, HeartbeatStatus::Timeout);
                }
            }
        }

        // 回调在外层触发，避免持锁调用
        for (const auto& [name, status] : changes) {
            fprintf(stderr, "[HeartbeatMonitor] 超时: %s\n", name.c_str());
            if (status_cb_) {
                HeartbeatState s{name, status, 0.0, default_timeout_sec_};
                status_cb_(s);
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace robot_runtime
