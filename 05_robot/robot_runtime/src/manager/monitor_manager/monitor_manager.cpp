#include "manager/monitor_manager/monitor_manager.h"
#include "manager/service_manager/service_manager.h"

#include <cstdio>
#include <chrono>
#include <thread>

namespace robot_runtime {

MonitorManager::MonitorManager(ServiceManager* sm, double check_interval)
    : sm_(sm)
    , check_interval_(check_interval)
{
}

MonitorManager::~MonitorManager() { stop(); }

void MonitorManager::start() {
    if (running_.exchange(true)) return;
    monitor_thread_ = std::thread(&MonitorManager::monitor_loop, this);
    printf("[MonitorManager] started (interval=%.1fs)\n", check_interval_);
}

void MonitorManager::stop() {
    if (!running_.exchange(false)) return;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    printf("[MonitorManager] stopped\n");
}

void MonitorManager::monitor_loop() {
    while (running_) {
        for (const auto& status : sm_->all_status()) {
            if (status.state == ServiceState::RUNNING && !status.alive) {
                auto now = std::chrono::system_clock::now();
                auto tt  = std::chrono::system_clock::to_time_t(now);
                fprintf(stderr, "[Monitor] [%s] exited unexpectedly (was PID=%d)\n",
                        status.name.c_str(), status.pid);
                failures_.push_back({status.name, static_cast<double>(tt), -1});
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(check_interval_ * 1000)));
    }
}

} // namespace robot_runtime
