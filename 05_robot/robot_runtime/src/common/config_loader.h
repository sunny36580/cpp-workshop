#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

// ============================================================================
// ServiceMeta — 服务目录内 service.yaml 规范
// ============================================================================
struct ServiceMeta {
    std::string name;
    std::string description;
    std::string type;         // ros2, python, cpp_binary, external
    std::string launch_cmd;
    std::string pid_file;
    std::string log_path;
};

inline ServiceMeta parse_service_meta(const std::string& filepath) {
    ServiceMeta meta;
    try {
        auto root = YAML::LoadFile(filepath);
        meta.name        = root["name"].as<std::string>("");
        meta.description = root["description"].as<std::string>("");
        meta.type        = root["type"].as<std::string>("ros2");
        meta.launch_cmd  = root["launch_cmd"].as<std::string>("");
        meta.pid_file    = root["pid_file"].as<std::string>("");
        meta.log_path    = root["log_path"].as<std::string>("");
    } catch (const std::exception&) {
    }
    return meta;
}

// ============================================================================
// ServiceConfig — 服务注册配置
// ============================================================================
struct ServiceConfig {
    std::string name;
    std::string path;
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
