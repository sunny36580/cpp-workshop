#include "runtime/core/service_manager.h"
#include "runtime/common/config_parser.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace robot_runtime {

// ============================================================================
// ServiceManager 实现
// ============================================================================
ServiceManager::ServiceManager(std::string workspace,
                               std::string config_dir,
                               std::string log_dir)
    : workspace_(std::move(workspace))
    , config_dir_(std::move(config_dir))
    , log_dir_(std::move(log_dir))
{
}

// ---------------------------------------------------------------------------
bool ServiceManager::load_config(const std::string& services_yaml) {
    std::string full_path = config_dir_ + "/" + services_yaml;

    auto configs = parse_services(full_path);
    for (auto& cfg : configs) {
        auto svc = std::make_shared<ProcessService>(
            cfg.name, cfg.path, cfg.depends, cfg.auto_restart);
        svc->set_workspace(workspace_);
        svc->set_log_dir(log_dir_);
        register_service(svc);
    }

    printf("[ServiceManager] loaded %zu services\n", services_.size());
    return true;
}

// ---------------------------------------------------------------------------
void ServiceManager::register_service(std::shared_ptr<ProcessService> svc) {
    services_[svc->name()] = std::move(svc);
}

// ---------------------------------------------------------------------------
std::shared_ptr<ProcessService> ServiceManager::get(const std::string& name) {
    auto it = services_.find(name);
    return (it != services_.end()) ? it->second : nullptr;
}

// ---------------------------------------------------------------------------
bool ServiceManager::do_start(const std::string& name) {
    auto svc = get(name);
    if (!svc) {
        fprintf(stderr, "[ServiceManager] unknown service: %s\n", name.c_str());
        return false;
    }

    // 先启动依赖
    for (const auto& dep_name : svc->depends()) {
        auto dep_svc = get(dep_name);
        if (dep_svc && dep_svc->state() != ServiceState::RUNNING) {
            printf("[%s] depends on [%s], starting first\n", name.c_str(), dep_name.c_str());
            if (!do_start(dep_name)) {
                fprintf(stderr, "[%s] dependency [%s] failed\n", name.c_str(), dep_name.c_str());
                return false;
            }
        }
    }

    return svc->start();
}

// ---------------------------------------------------------------------------
bool ServiceManager::do_stop(const std::string& name) {
    auto svc = get(name);
    if (!svc) {
        fprintf(stderr, "[ServiceManager] unknown service: %s\n", name.c_str());
        return false;
    }

    // 先停止依赖此服务的其他服务
    for (const auto& [other_name, other_svc] : services_) {
        if (other_name != name && other_svc->state() == ServiceState::RUNNING) {
            const auto& deps = other_svc->depends();
            if (std::find(deps.begin(), deps.end(), name) != deps.end()) {
                printf("[%s] stopping dependent [%s] first\n", name.c_str(), other_name.c_str());
                do_stop(other_name);
            }
        }
    }

    return svc->stop();
}

// ---------------------------------------------------------------------------
bool ServiceManager::start(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return do_start(name);
}

// ---------------------------------------------------------------------------
bool ServiceManager::stop(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return do_stop(name);
}

// ---------------------------------------------------------------------------
bool ServiceManager::restart(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto svc = get(name);
    if (!svc) return false;
    return svc->restart();
}

// ---------------------------------------------------------------------------
void ServiceManager::start_all() {
    for (const auto& [name, _] : services_) {
        start(name);
    }
}

// ---------------------------------------------------------------------------
void ServiceManager::stop_all() {
    auto order = resolve_start_order();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        stop(*it);
    }
}

// ---------------------------------------------------------------------------
void ServiceManager::start_group(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (n == "all") { start_all(); }
        else            { start(n); }
    }
}

// ---------------------------------------------------------------------------
void ServiceManager::stop_group(const std::vector<std::string>& names) {
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (*it == "all") { stop_all(); }
        else              { stop(*it); }
    }
}

// ---------------------------------------------------------------------------
std::vector<ServiceStatus> ServiceManager::all_status() const {
    std::vector<ServiceStatus> result;
    for (const auto& [name, svc] : services_) {
        result.push_back(svc->status());
    }
    return result;
}

// ---------------------------------------------------------------------------
std::vector<std::string> ServiceManager::resolve_start_order() const {
    // 拓扑排序（DFS）
    std::vector<std::string> order;
    std::unordered_map<std::string, bool> visited;

    std::function<void(const std::string&)> dfs = [&](const std::string& name) {
        if (visited[name]) return;
        visited[name] = true;
        auto it = services_.find(name);
        if (it != services_.end()) {
            for (const auto& dep : it->second->depends()) {
                dfs(dep);
            }
            order.push_back(name);
        }
    };

    for (const auto& [name, _] : services_) {
        dfs(name);
    }

    return order;
}

} // namespace robot_runtime
