#include "runtime/managers/mode_manager.h"
#include "runtime/core/service_manager.h"

#include <cstdio>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

// ============================================================================
// ModeManager 实现
// ============================================================================
ModeManager::ModeManager(std::string config_dir, ServiceManager* sm)
    : config_dir_(std::move(config_dir))
    , sm_(sm)
{
}

// ---------------------------------------------------------------------------
bool ModeManager::load_config(const std::string& modes_yaml) {
    std::string full_path = config_dir_ + "/" + modes_yaml;
    YAML::Node root;

    try {
        root = YAML::LoadFile(full_path);
    } catch (const std::exception& e) {
        fprintf(stderr, "[ModeManager] failed to load config: %s\n", e.what());
        return false;
    }

    auto modes_cfg = root["modes"];
    if (!modes_cfg) {
        fprintf(stderr, "[ModeManager] no 'modes' key in config\n");
        return false;
    }

    for (const auto& entry : modes_cfg) {
        auto key = entry.first.as<std::string>();

        if (key == "default") {
            default_mode_ = entry.second.as<std::string>();
            continue;
        }

        std::vector<std::string> services;
        for (const auto& svc : entry.second["services"]) {
            services.push_back(svc.as<std::string>());
        }
        modes_[key] = std::move(services);
    }

    printf("[ModeManager] loaded %zu modes (default=%s)\n",
           modes_.size(), default_mode_.c_str());
    return true;
}

// ---------------------------------------------------------------------------
bool ModeManager::switch_to(const std::string& mode_name) {
    auto it = modes_.find(mode_name);
    if (it == modes_.end()) {
        fprintf(stderr, "[ModeManager] unknown mode: %s\n", mode_name.c_str());
        return false;
    }

    const auto& target_services = it->second;
    printf("[ModeManager] switching to mode [%s]\n", mode_name.c_str());

    // 用 set 方便做差集
    std::unordered_set<std::string> target_set(target_services.begin(), target_services.end());
    bool has_all = target_set.count("all") > 0;

    std::unordered_set<std::string> current_set;
    if (!current_mode_.empty()) {
        auto cur_it = modes_.find(current_mode_);
        if (cur_it != modes_.end()) {
            current_set = std::unordered_set<std::string>(
                cur_it->second.begin(), cur_it->second.end());
        }
    }

    // 1) 计算需要停止的服务
    std::vector<std::string> to_stop;
    for (const auto& s : current_set) {
        if (!target_set.count(s) && !has_all) {
            to_stop.push_back(s);
        }
    }

    // 2) 计算需要启动的服务
    std::vector<std::string> to_start;
    if (has_all) {
        // all 表示启动全部服务
        for (const auto& [name, _] : sm_->services()) {
            to_start.push_back(name);
        }
    } else {
        for (const auto& s : target_set) {
            if (!current_set.count(s)) {
                to_start.push_back(s);
            }
        }
    }

    // 3) 停止不需要的服务
    for (const auto& name : to_stop) {
        sm_->stop(name);
    }

    // 4) 启动新模式需要的服务
    for (const auto& name : to_start) {
        sm_->start(name);
    }

    current_mode_ = mode_name;
    printf("[ModeManager] switched to mode [%s]\n", mode_name.c_str());
    return true;
}

// ---------------------------------------------------------------------------
void ModeManager::apply_default() {
    switch_to(default_mode_);
}

} // namespace robot_runtime
