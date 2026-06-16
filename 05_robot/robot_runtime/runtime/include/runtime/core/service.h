#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <optional>

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
    ServiceState state;
    int pid = 0;
    bool alive = false;
    std::vector<std::string> depends;
};

// ============================================================================
// IService — 服务接口（为未来 AimRT Module 做准备）
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
// Runtime 不感知服务类型，所有服务统一：
//   start: cd path && bash start.sh
//   stop:  cd path && bash stop.sh || kill PID
// ============================================================================
class ProcessService : public IService {
public:
    ProcessService(std::string name,
                   std::string path,
                   std::vector<std::string> depends = {},
                   bool auto_restart = false);
    ~ProcessService() override;

    // IService 接口
    bool start() override;
    bool stop()  override;
    bool restart() override;
    ServiceState state() const override { return state_; }

    // 进程
    bool is_alive() const;
    int  pid() const { return pid_; }

    // 配置
    void set_workspace(std::string ws) { workspace_ = std::move(ws); }
    void set_log_dir(std::string dir)  { log_dir_   = std::move(dir); }

    const std::string& name()    const { return name_; }
    const std::string& path()    const { return path_; }
    const std::vector<std::string>& depends() const { return depends_; }
    bool auto_restart() const { return auto_restart_; }

    ServiceStatus status() const;

private:
    std::string name_;
    std::string path_;
    std::vector<std::string> depends_;
    bool auto_restart_ = false;
    ServiceState state_ = ServiceState::STOPPED;
    pid_t pid_ = 0;
    pid_t pgid_ = 0;  // 进程组 ID，用于批量清理
    std::string workspace_;
    std::string log_dir_;
};

} // namespace robot_runtime
