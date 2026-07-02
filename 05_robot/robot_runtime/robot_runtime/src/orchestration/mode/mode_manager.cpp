#include "orchestration/mode/mode_manager.h"
#include "runtime/process/service_manager/service_manager.h"

#include <cstdio>
#include <unordered_set>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

ModeManager::ModeManager(std::string config_dir, ServiceManager* sm)
    : config_dir_(std::move(config_dir))
    , sm_(sm)
{
}

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

    auto [mode_configs, def] = parse_modes(full_path);
    default_mode_ = def;
    for (auto& mc : mode_configs) {
        modes_[mc.name] = std::move(mc.services);
    }

    printf("[ModeManager] loaded %zu modes (default=%s)\n",
           modes_.size(), default_mode_.c_str());
    return true;
}

bool ModeManager::switch_to(const std::string& mode_name) {
    auto it = modes_.find(mode_name);
    if (it == modes_.end()) {
        fprintf(stderr, "[ModeManager] unknown mode: %s\n", mode_name.c_str());
        return false;
    }

    const auto& target_entries = it->second;
    printf("[ModeManager] switching to mode [%s]\n", mode_name.c_str());

    // ---- 构造当前模式的目标状态索引 ----
    // 每个条目包含服务名 + 目标状态 (active/inactive/stopped)
    std::unordered_map<std::string, ModeTargetState> target_map;
    bool has_all = false;
    for (const auto& entry : target_entries) {
        if (entry.name == "all") {
            has_all = true;
            break;
        }
        target_map[entry.name] = entry.target;
    }

    // ---- 计算 diff ----
    // 只管理 lifecycle != External 的服务
    auto is_managed = [this](const std::string& name) -> bool {
        auto svc = sm_->get(name);
        if (!svc) return true;
        return svc->config().lifecycle != ServiceLifecycle::External;
    };

    // 停止：当前在 target_map 中不存在 或 目标为 stopped 的服务
    std::vector<std::string> to_stop;
    if (!has_all) {
        for (const auto& [name, svc] : sm_->services()) {
            if (!is_managed(name)) continue;
            auto tit = target_map.find(name);
            if (tit == target_map.end() || tit->second == ModeTargetState::Stopped) {
                if (svc->state() != ServiceState::STOPPED) {
                    to_stop.push_back(name);
                }
            }
        }
    }

    // 启动：目标为 active/inactive 且当前未运行的服务
    std::vector<std::string> to_start;
    if (has_all) {
        for (const auto& [name, svc] : sm_->services()) {
            if (is_managed(name) && svc->state() != ServiceState::RUNNING) {
                to_start.push_back(name);
            }
        }
    } else {
        for (const auto& [name, target] : target_map) {
            if (!is_managed(name)) continue;
            if (target == ModeTargetState::Stopped) continue;
            auto svc = sm_->get(name);
            if (!svc || svc->state() != ServiceState::RUNNING) {
                to_start.push_back(name);
            }
        }
    }

    // 执行
    if (!to_stop.empty()) {
        printf("[ModeManager]  stopping %zu services\n", to_stop.size());
        for (const auto& name : to_stop) sm_->stop(name);
    }
    if (!to_start.empty()) {
        printf("[ModeManager]  starting %zu services\n", to_start.size());
        for (const auto& name : to_start) sm_->start(name);
    }

    current_mode_ = mode_name;
    int kept = static_cast<int>(target_map.size()) - static_cast<int>(to_start.size());
    printf("[ModeManager] mode [%s] applied (started=%d, stopped=%zu)\n",
           mode_name.c_str(), static_cast<int>(to_start.size()), to_stop.size());
    return true;
}

void ModeManager::apply_default() {
    switch_to(default_mode_);
}

} // namespace robot_runtime
