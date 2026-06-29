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

        auto fhb = mon["file_heartbeat"];
        if (fhb && fhb["enabled"].as<bool>(false)) {
            file_hb_enabled_ = true;
            if (fhb["dir"])        file_hb_detector_.set_heartbeat_dir(fhb["dir"].as<std::string>());
            if (fhb["timeout_sec"]) file_hb_detector_.set_timeout_sec(fhb["timeout_sec"].as<double>());

            // 启动时自动清空心跳目录，清理历史残留
            file_hb_detector_.create_dir();
            file_hb_detector_.clear_dir();

            printf("[MonitorManager] 文件心跳检测: dir=%s timeout=%.1fs\n",
                   file_hb_detector_.heartbeat_dir().c_str(),
                   file_hb_detector_.timeout_sec());
        }
    } catch (const std::exception& e) {
        fprintf(stderr, "[MonitorManager] 加载监控配置失败: %s\n", e.what());
    }
}

void MonitorManager::start() {
    if (running_.exchange(true)) return;
    monitor_thread_ = std::thread(&MonitorManager::monitor_loop, this);
    printf("[MonitorManager] started (interval=%.1fs, file_hb=%s)\n",
           check_interval_, file_hb_enabled_ ? "on" : "off");
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
            // ① ProcessDetector：进程 PID 是否存活（进程崩溃）
            bool process_dead = (status.state == ServiceState::RUNNING && !status.alive);

            // ② FileHeartbeatDetector：心跳文件 mtime 是否超时（进程存活但业务卡死）
            bool hb_timeout = (file_hb_enabled_ &&
                               status.state == ServiceState::RUNNING &&
                               file_hb_detector_.check(status.name));

            if (process_dead || hb_timeout) {
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
