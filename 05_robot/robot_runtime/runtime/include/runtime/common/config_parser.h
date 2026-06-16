#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

// ============================================================================
// ConfigParser — YAML 配置解析工具
// ============================================================================
struct ServiceConfig {
    std::string name;
    std::string path;        // 服务目录（相对于工作区）
    std::vector<std::string> depends;
    bool auto_restart = false;
};

struct ModeConfig {
    std::string name;
    std::vector<std::string> services;
};

struct RuntimeConfig {
    std::vector<ServiceConfig> services;
    std::vector<ModeConfig> modes;
    std::string default_mode = "standby";
};

// 从 services.yaml 解析服务注册信息
inline std::vector<ServiceConfig> parse_services(const std::string& filepath) {
    std::vector<ServiceConfig> result;
    auto root = YAML::LoadFile(filepath);
    auto svc_cfg = root["services"];
    if (!svc_cfg) return result;

    for (const auto& entry : svc_cfg) {
        ServiceConfig cfg;
        cfg.name = entry.first.as<std::string>();
        cfg.path  = entry.second["path"].as<std::string>("");
        cfg.auto_restart = entry.second["auto_restart"].as<bool>(false);

        if (entry.second["depends"]) {
            for (const auto& dep : entry.second["depends"]) {
                cfg.depends.push_back(dep.as<std::string>());
            }
        }

        if (!cfg.name.empty() && !cfg.path.empty()) {
            result.push_back(std::move(cfg));
        }
    }
    return result;
}

// 从 modes.yaml 解析模式配置
inline std::pair<std::vector<ModeConfig>, std::string> parse_modes(const std::string& filepath) {
    std::vector<ModeConfig> result;
    std::string default_mode = "standby";
    auto root = YAML::LoadFile(filepath);
    auto modes_cfg = root["modes"];
    if (!modes_cfg) return {result, default_mode};

    for (const auto& entry : modes_cfg) {
        auto key = entry.first.as<std::string>();
        if (key == "default") {
            default_mode = entry.second.as<std::string>();
            continue;
        }

        ModeConfig cfg;
        cfg.name = key;
        for (const auto& svc : entry.second["services"]) {
            cfg.services.push_back(svc.as<std::string>());
        }
        result.push_back(std::move(cfg));
    }
    return {result, default_mode};
}

} // namespace robot_runtime
