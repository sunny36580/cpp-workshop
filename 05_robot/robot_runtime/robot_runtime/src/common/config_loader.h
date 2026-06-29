#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

// ============================================================================
// ServiceConfig — 服务注册配置（来自 config/services.yaml）
// ============================================================================
// 所有服务元信息统一放在 config/services.yaml 中，每个服务目录下不再维护
// 独立的 service.yaml 文件，避免配置分散、信息不一致。
// ============================================================================
struct ServiceConfig {
    std::string name;
    std::string path;
    std::string description;
    std::string type;         // ros2, python, cpp_binary, external
    std::string launch_cmd;
    std::string pid_file;
    std::string log_path;
    std::vector<std::string> depends;
    bool auto_restart = false;
};

struct ModeConfig {
    std::string name;
    std::vector<std::string> services;
};

inline std::vector<ServiceConfig> parse_services(const std::string& filepath) {
    std::vector<ServiceConfig> result;
    auto root = YAML::LoadFile(filepath);
    auto svc_cfg = root["services"];
    if (!svc_cfg) return result;

    for (const auto& entry : svc_cfg) {
        ServiceConfig cfg;
        cfg.name        = entry.first.as<std::string>();
        cfg.path        = entry.second["path"].as<std::string>("");
        cfg.description = entry.second["description"].as<std::string>("");
        cfg.type        = entry.second["type"].as<std::string>("ros2");
        cfg.launch_cmd  = entry.second["launch_cmd"].as<std::string>("");
        cfg.pid_file    = entry.second["pid_file"].as<std::string>("");
        cfg.log_path    = entry.second["log_path"].as<std::string>("");
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
