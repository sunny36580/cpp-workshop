/**
 * @file service_manager.h
 * @brief 进程级服务启停管理
 * @role runtime/process
 */
#pragma once

#include "common/config_loader.h"
#include "common/process_utils.h"
#include "common/type_def.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <chrono>

namespace robot_runtime {

enum class ServiceState {
    STOPPED,
    STARTING,
    RUNNING,
    STOPPING,
    FAILED
};

inline const char* to_string(ServiceState s) {
    switch (s) {
        case ServiceState::STOPPED:  return "stopped";
        case ServiceState::STARTING: return "starting";
        case ServiceState::RUNNING:  return "running";
        case ServiceState::STOPPING: return "stopping";
        case ServiceState::FAILED:   return "failed";
    }
    return "unknown";
}

/**
 * @brief 对外暴露的服务状态快照
 */
struct ServiceStatus {
    std::string name;
    std::string description;
    std::string type;           // ros2/python/cpp_binary
    ServiceState state;
    pid_t pid = 0;              // 进程 PID
    bool alive = false;         // 进程是否存活
    std::vector<std::string> depends;
};

/**
 * @class IService
 * @brief 服务接口
 * @responsibility 定义服务的基本操作接口
 */
class IService {
public:
    virtual ~IService() = default;
    virtual bool start() = 0;
    virtual bool stop()  = 0;
    virtual bool restart() = 0;
    virtual ServiceState state() const = 0;
};

/**
 * @class ProcessService
 * @brief 进程级服务实现
 * @responsibility fork+exec 启动外部进程，通过 PID 管理生命周期
 */
class ProcessService : public IService {
public:
    /**
     * @param name 服务名
     * @param path 服务目录
     * @param depends 依赖列表
     * @param auto_restart 崩溃后自动重启
     */
    ProcessService(std::string name, std::string path,
                   std::vector<std::string> depends = {},
                   bool auto_restart = false);
    ~ProcessService() override;

    bool start() override;
    bool stop()  override;
    bool restart() override;
    ServiceState state() const override { return state_; }

    bool is_alive() const;
    pid_t pid() const { return pid_; }

    void set_workspace(std::string ws) { workspace_ = std::move(ws); }
    void set_log_dir(std::string dir)  { log_dir_   = std::move(dir); }
    void set_config(ServiceConfig cfg) { cfg_ = std::move(cfg); }

    const std::string& name()    const { return name_; }
    const std::string& path()    const { return path_; }
    const std::vector<std::string>& depends() const { return depends_; }
    bool auto_restart() const { return auto_restart_; }
    const ServiceConfig& config() const { return cfg_; }

    ServiceStatus status() const;

private:
    std::string normalize_path() const;

    std::string name_;                      // 服务名
    std::string path_;                      // 服务目录路径
    std::vector<std::string> depends_;       // 依赖的服务名列表
    bool auto_restart_ = false;              // 崩溃后自动重启
    ServiceState state_ = ServiceState::STOPPED;
    pid_t pid_ = 0;                          // 子进程 PID
    pid_t pgid_ = 0;                         // 子进程组 ID
    ServiceConfig cfg_;                      // 配置文件快照
    std::string workspace_;                  // 工作目录
    std::string log_dir_;                    // 日志输出目录
};

/**
 * @class ServiceManager
 * @brief 服务注册、启停、依赖解析
 * @responsibility 管理所有外部服务的生命周期
 */
class ServiceManager {
public:
    /**
     * @param workspace 工作目录
     * @param config_dir 配置目录
     * @param log_dir 日志目录
     */
    ServiceManager(std::string workspace,
                   std::string config_dir,
                   std::string log_dir);

    /**
     * @brief 加载 services.yaml 配置文件
     * @param services_yaml services.yaml 文件名
     * @return true=加载成功
     */
    bool load_config(const std::string& services_yaml);

    /**
     * @brief 注册一个已创建的服务实例
     * @param svc 服务实例
     */
    void register_service(std::shared_ptr<ProcessService> svc);

    bool start(const std::string& name);

    bool stop(const std::string& name);
    bool restart(const std::string& name);
    void start_all();
    void stop_all();
    void start_group(const std::vector<std::string>& names);
    void stop_group(const std::vector<std::string>& names);

    std::shared_ptr<ProcessService> get(const std::string& name);
    std::vector<ServiceStatus> all_status() const;
    std::vector<std::string> resolve_start_order() const;

    const auto& services() const { return services_; }

private:
    bool do_start(const std::string& name);
    bool do_stop(const std::string& name);

    std::string workspace_;
    std::string config_dir_;
    std::string log_dir_;
    std::unordered_map<std::string, std::shared_ptr<ProcessService>> services_;
    std::mutex mutex_;
};

} // namespace robot_runtime
