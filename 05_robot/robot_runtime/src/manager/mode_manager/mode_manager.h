#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace robot_runtime {

class ServiceManager;

class ModeManager {
public:
    ModeManager(std::string config_dir, ServiceManager* sm);

    bool load_config(const std::string& modes_yaml);
    bool switch_to(const std::string& mode_name);
    void apply_default();

    const std::string& current_mode() const { return current_mode_; }
    const std::string& default_mode() const { return default_mode_; }
    const auto& modes() const { return modes_; }

private:
    std::string config_dir_;
    ServiceManager* sm_ = nullptr;
    std::unordered_map<std::string, std::vector<std::string>> modes_;
    std::string current_mode_;
    std::string default_mode_ = "standby";
};

} // namespace robot_runtime
