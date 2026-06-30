#include "runtime/monitor/monitor_manager.h"
#include "runtime/process/service_manager/service_manager.h"

#include <cstdio>
#include <chrono>
#include <thread>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

MonitorManager::MonitorManager(ServiceManager* sm, double check_interval)
    : sm_(sm)
    , check_interval_(check_interval)
{
}

MonitorManager::~MonitorManager() { stop(); }

void MonitorManager::load_config(const std::string& config_path) {
    try {
        auto root = YAML::LoadFile(config_path);
        auto mon = root["monitor"];
        if (!mon) return;

        if (mon["check_interval"]) {
            check_interval_ = mon["check_interval"].as<double>();
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[MonitorManager] 加载监控配置失败: %s\n", e.what());
    }
}

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
            // ProcessDetector：进程 PID 是否存活（进程崩溃）
            bool process_dead = (status.state == ServiceState::RUNNING && !status.alive);

            if (process_dead) {
                auto now = std::chrono::system_clock::now();
                auto tt  = std::chrono::system_clock::to_time_t(now);
                fprintf(stderr, "[Monitor] [%s] %s\n",
                        status.name.c_str(),
                        process_dead ?
                            "进程已退出" :
                            "心跳超时（进程存活但无响应）");
                failures_.push_back({status.name, static_cast<double>(tt), -1});

                // TODO: 共用自愈策略（重启 / 降级 / 告警）
            }
        }
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(check_interval_ * 1000)));
    }
}

} // namespace robot_runtime
