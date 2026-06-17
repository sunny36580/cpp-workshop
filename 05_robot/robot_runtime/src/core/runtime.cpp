#include "core/runtime.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace robot_runtime {

Runtime::Runtime(std::string workspace_dir,
                 std::string config_dir,
                 std::string log_dir)
    : workspace_dir_(std::move(workspace_dir))
    , config_dir_(std::move(config_dir))
    , log_dir_(std::move(log_dir))
{
}

Runtime::~Runtime() {
    if (mon_) mon_->stop();
}

bool Runtime::init() {
    fs::create_directories(log_dir_);
    fs::create_directories(log_dir_ + "/services");

    sm_ = std::make_unique<ServiceManager>(workspace_dir_, config_dir_, log_dir_);
    mm_ = std::make_unique<ModeManager>(config_dir_, sm_.get());
    mon_ = std::make_unique<MonitorManager>(sm_.get());
    dm_ = std::make_unique<DependencyManager>();

    if (!sm_->load_config("services.yaml")) {
        fprintf(stderr, "[Runtime] failed to load services.yaml\n");
        return false;
    }
    if (!mm_->load_config("modes.yaml")) {
        fprintf(stderr, "[Runtime] failed to load modes.yaml\n");
        return false;
    }

    for (const auto& [name, svc] : sm_->services()) {
        dm_->add_dependency(name, svc->depends());
    }

    mon_->start();
    printf("[Runtime] initialized\n");
    return true;
}

bool Runtime::start_service(const std::string& name) { return sm_->start(name); }
bool Runtime::stop_service(const std::string& name)  { return sm_->stop(name); }
bool Runtime::restart_service(const std::string& name) { return sm_->restart(name); }
void Runtime::start_all() { sm_->start_all(); }
void Runtime::stop_all()  { sm_->stop_all(); }

bool Runtime::switch_mode(const std::string& mode_name) { return mm_->switch_to(mode_name); }
void Runtime::apply_default_mode() { mm_->apply_default(); }

std::vector<ServiceStatus> Runtime::all_status() const { return sm_->all_status(); }
std::shared_ptr<ProcessService> Runtime::get_service(const std::string& name) const {
    return sm_->get(name);
}

} // namespace robot_runtime
