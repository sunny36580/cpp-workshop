#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

namespace robot_runtime {

class ServiceManager;

class MonitorManager {
public:
    explicit MonitorManager(ServiceManager* sm, double check_interval = 3.0);
    ~MonitorManager();

    void start();
    void stop();

    /// 从 YAML 加载监控配置
    void load_config(const std::string& config_path);

    struct FailureRecord {
        std::string name;
        double time;
        int exit_code;
    };

    std::vector<FailureRecord> failures() const { return failures_; }
    void clear_failures() { failures_.clear(); }

private:
    void monitor_loop();

    ServiceManager* sm_ = nullptr;
    double check_interval_ = 3.0;
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    std::vector<FailureRecord> failures_;
};

} // namespace robot_runtime
