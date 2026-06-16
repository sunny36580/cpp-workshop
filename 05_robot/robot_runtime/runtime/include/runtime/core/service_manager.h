#pragma once

#include "runtime/core/service.h"
#include "runtime/managers/mode_manager.h"
#include "runtime/managers/monitor_manager.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>

namespace robot_runtime {

// ============================================================================
// ServiceManager — 管理所有服务的注册、启停、依赖解析
// ============================================================================
class ServiceManager {
public:
    ServiceManager(std::string workspace,
                   std::string config_dir,
                   std::string log_dir);

    bool load_config(const std::string& services_yaml);
    void register_service(std::shared_ptr<ProcessService> svc);

    // 启停
    bool start(const std::string& name);
    bool stop(const std::string& name);
    bool restart(const std::string& name);
    void start_all();
    void stop_all();
    void start_group(const std::vector<std::string>& names);
    void stop_group(const std::vector<std::string>& names);

    // 查询
    std::shared_ptr<ProcessService> get(const std::string& name);
    std::vector<ServiceStatus> all_status() const;
    std::vector<std::string> resolve_start_order() const;

    // 依赖关系
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
