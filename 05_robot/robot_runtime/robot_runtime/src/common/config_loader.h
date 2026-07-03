/**
 * @file config_loader.h
 * @brief YAML 配置解析
 * @role common
 */
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

namespace robot_runtime {

/** @brief 服务生命周期策略 */
enum class ServiceLifecycle {
    Simple,
    Managed,
    External,
};

inline const char* to_string(ServiceLifecycle lc) {
    switch (lc) {
        case ServiceLifecycle::Simple:   return "simple";
        case ServiceLifecycle::Managed:  return "managed";
        case ServiceLifecycle::External: return "external";
    }
    return "simple";
}

inline ServiceLifecycle parse_lifecycle(const YAML::Node& node) {
    if (!node) return ServiceLifecycle::Simple;
    auto val = node.as<std::string>("simple");
    if (val == "managed")   return ServiceLifecycle::Managed;
    if (val == "external")  return ServiceLifecycle::External;
    return ServiceLifecycle::Simple;
}

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
    ServiceLifecycle lifecycle = ServiceLifecycle::Simple;
};

/// 模式下某个服务的目标状态
enum class ModeTargetState {
    Active,     /// 激活（process up + capability ready）
    Inactive,   /// 停用（process up but capability suspended）
    Stopped,    /// 停止（process down）
};

/// 模式下每个服务的预期状态
struct ModeServiceEntry {
    std::string name;
    ModeTargetState target = ModeTargetState::Active;
};

struct ModeConfig {
    std::string name;
    std::vector<ModeServiceEntry> services;
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
        cfg.lifecycle   = parse_lifecycle(entry.second["lifecycle"]);

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

        auto svcs = entry.second["services"];
        if (svcs && svcs.IsMap()) {
            // 新格式: service_name: target_state
            for (const auto& kv : svcs) {
                ModeServiceEntry e;
                e.name   = kv.first.as<std::string>();
                auto st  = kv.second.as<std::string>("active");
                if (st == "inactive")  e.target = ModeTargetState::Inactive;
                else if (st == "stopped") e.target = ModeTargetState::Stopped;
                else e.target = ModeTargetState::Active;
                cfg.services.push_back(std::move(e));
            }
        } else if (svcs && svcs.IsSequence()) {
            // 兼容旧格式: [service1, service2] → 全部视为 active
            for (const auto& item : svcs) {
                ModeServiceEntry e;
                e.name   = item.as<std::string>();
                e.target = ModeTargetState::Active;
                cfg.services.push_back(std::move(e));
            }
        }

        result.push_back(std::move(cfg));
    }
    return {result, default_mode};
}

} // namespace robot_runtime
