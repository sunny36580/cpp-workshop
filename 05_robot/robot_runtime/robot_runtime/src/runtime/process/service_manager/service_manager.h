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

// ============================================================================
// 服务状态枚举
// ============================================================================
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

// ============================================================================
// ServiceStatus — 对外暴露的状态快照
// ============================================================================
struct ServiceStatus {
    std::string name;
    std::string description;
    std::string type;
    ServiceState state;
    pid_t pid = 0;
    bool alive = false;
    std::vector<std::string> depends;
};

// ============================================================================
// IService — 服务接口
// ============================================================================
class IService {
public:
    virtual ~IService() = default;
    virtual bool start() = 0;
    virtual bool stop()  = 0;
    virtual bool restart() = 0;
    virtual ServiceState state() const = 0;
};

// ============================================================================
// ProcessService — 进程级服务实现
// ============================================================================
class ProcessService : public IService {
public:
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

    std::string name_;
    std::string path_;
    std::vector<std::string> depends_;
    bool auto_restart_ = false;
    ServiceState state_ = ServiceState::STOPPED;
    pid_t pid_ = 0;
    pid_t pgid_ = 0;
    ServiceConfig cfg_;
    std::string workspace_;
    std::string log_dir_;
};

// ============================================================================
// ServiceManager — 服务注册、启停、依赖解析
// ============================================================================
class ServiceManager {
public:
    ServiceManager(std::string workspace,
                   std::string config_dir,
                   std::string log_dir);

    bool load_config(const std::string& services_yaml);
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
