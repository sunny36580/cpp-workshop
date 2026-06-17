#pragma once

#include "manager/service_manager/service_manager.h"
#include "manager/mode_manager/mode_manager.h"
#include "manager/monitor_manager/monitor_manager.h"
#include "manager/dependency_manager/dependency_manager.h"

#include <string>
#include <memory>

namespace robot_runtime {

class Runtime {
public:
    Runtime(std::string workspace_dir,
            std::string config_dir,
            std::string log_dir);
    ~Runtime();

    bool init();

    bool start_service(const std::string& name);
    bool stop_service(const std::string& name);
    bool restart_service(const std::string& name);
    void start_all();
    void stop_all();

    bool switch_mode(const std::string& mode_name);
    void apply_default_mode();

    std::vector<ServiceStatus> all_status() const;
    std::shared_ptr<ProcessService> get_service(const std::string& name) const;

    ServiceManager& service_manager() { return *sm_; }
    ModeManager&    mode_manager()    { return *mm_; }
    MonitorManager& monitor_manager() { return *mon_; }

private:
    std::string workspace_dir_;
    std::string config_dir_;
    std::string log_dir_;

    std::unique_ptr<ServiceManager> sm_;
    std::unique_ptr<ModeManager>    mm_;
    std::unique_ptr<MonitorManager> mon_;
    std::unique_ptr<DependencyManager> dm_;
};

} // namespace robot_runtime
