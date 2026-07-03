/**
 * @file monitor_manager.h
 * @brief 进程级监控器
 * @role runtime/monitor
 */
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <thread>

namespace robot_runtime {

class ServiceManager;

/**
 * @class MonitorManager
 * @brief 进程存活巡检器
 * @responsibility 定时检查进程 PID、记录故障
 */
class MonitorManager {
public:
    /**
     * @brief 构造函数
     * @param sm 服务管理器
     * @param check_interval 巡检间隔(秒)
     */
    explicit MonitorManager(ServiceManager* sm, double check_interval = 3.0);
    ~MonitorManager();

    /**
     * @brief 启动巡检
     */
    void start();
    /**
     * @brief 停止巡检
     */
    void stop();

    /**
     * @brief 加载监控配置
     * @param config_path monitor.yaml 路径
     */
    void load_config(const std::string& config_path);

    struct FailureRecord {
        std::string name;
        double time;
        int exit_code;
    };

    /**
     * @return 历史故障记录
     */
    std::vector<FailureRecord> failures() const { return failures_; }
    void clear_failures() { failures_.clear(); }

private:
    void monitor_loop();

    ServiceManager* sm_ = nullptr;       // 服务管理器
    double check_interval_ = 3.0;        // 巡检间隔(秒)
    std::atomic<bool> running_{false};
    std::thread monitor_thread_;
    std::vector<FailureRecord> failures_;  // 故障历史
};

} // namespace robot_runtime
