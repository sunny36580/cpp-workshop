#include "manager/service_manager/service_manager.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <chrono>
#include <ctime>
#include <fstream>
#include <algorithm>
#include <functional>

namespace robot_runtime {

// ============================================================================
// ProcessService 实现
// ============================================================================
ProcessService::ProcessService(std::string name,
                               std::string path,
                               std::vector<std::string> depends,
                               bool auto_restart)
    : name_(std::move(name))
    , path_(std::move(path))
    , depends_(std::move(depends))
    , auto_restart_(auto_restart)
{
}

ProcessService::~ProcessService() {
    if (state_ == ServiceState::RUNNING) {
        stop();
    }
}

std::string ProcessService::normalize_path() const {
    if (!path_.empty() && path_[0] == '/') {
        return path_;
    } else if (!workspace_.empty() && workspace_ != ".") {
        return workspace_ + "/" + path_;
    } else {
        return path_;
    }
}

bool ProcessService::start() {
    if (state_ == ServiceState::RUNNING) return true;

    std::string abs_path = normalize_path();
    std::string cmd = "cd " + abs_path + " && bash start.sh";
    std::string log_path = log_dir_ + "/services/" + name_ + ".log";

    state_ = ServiceState::STARTING;

    pid_t child_pgid = 0;
    pid_t pid = fork_and_exec(abs_path, cmd, log_path, &child_pgid);
    if (pid < 0) {
        fprintf(stderr, "[%s] fork failed\n", name_.c_str());
        state_ = ServiceState::FAILED;
        return false;
    }

    pid_ = pid;
    pgid_ = child_pgid;
    state_ = ServiceState::RUNNING;
    printf("[%s] started (PID=%d, PGID=%d)\n", name_.c_str(), pid_, pgid_);
    return true;
}

bool ProcessService::stop() {
    if (pid_ <= 0) {
        state_ = ServiceState::STOPPED;
        return true;
    }

    printf("[%s] stopping (PID=%d)\n", name_.c_str(), pid_);
    state_ = ServiceState::STOPPING;

    // 1) 先尝试 stop.sh
    std::string abs_path = normalize_path();
    std::string stop_script = abs_path + "/stop.sh";
    if (access(stop_script.c_str(), X_OK) == 0) {
        std::string cmd = "cd " + abs_path + " && bash stop.sh";
        pid_t stop_pid = fork_and_exec(abs_path, cmd, "");
        if (stop_pid > 0) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                int status;
                if (waitpid(stop_pid, &status, WNOHANG) == stop_pid) break;
                usleep(100000);
            }
        }
    }

    // 2) SIGTERM → 3s → SIGKILL
    stop_process_group(pid_, pgid_);
    pid_ = 0;
    pgid_ = 0;
    state_ = ServiceState::STOPPED;
    printf("[%s] stopped\n", name_.c_str());
    return true;
}

bool ProcessService::restart() {
    stop();
    return start();
}

bool ProcessService::is_alive() const {
    return is_pid_alive(pid_);
}

ServiceStatus ProcessService::status() const {
    ServiceStatus s;
    s.name        = name_;
    s.description = meta_.description;
    s.type        = meta_.type;
    s.state       = state_;
    s.pid         = is_alive() ? pid_ : 0;
    s.alive       = is_alive();
    s.depends     = depends_;
    return s;
}

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

bool ServiceManager::load_config(const std::string& services_yaml) {
    std::string full_path = config_dir_ + "/" + services_yaml;

    auto configs = parse_services(full_path);
    for (auto& cfg : configs) {
        if (cfg.path.size() >= 2 && cfg.path[0] == '.' && cfg.path[1] == '/') {
            cfg.path = cfg.path.substr(2);
        }
        auto svc = std::make_shared<ProcessService>(
            cfg.name, cfg.path, cfg.depends, cfg.auto_restart);
        svc->set_workspace(workspace_);
        svc->set_log_dir(log_dir_);

        std::string svc_dir = workspace_ + "/" + cfg.path;
        ServiceMeta meta = parse_service_meta(svc_dir + "/service.yaml");
        svc->set_meta(meta);

        register_service(svc);
    }

    printf("[ServiceManager] loaded %zu services\n", services_.size());
    return true;
}

void ServiceManager::register_service(std::shared_ptr<ProcessService> svc) {
    services_[svc->name()] = std::move(svc);
}

std::shared_ptr<ProcessService> ServiceManager::get(const std::string& name) {
    auto it = services_.find(name);
    return (it != services_.end()) ? it->second : nullptr;
}

bool ServiceManager::do_start(const std::string& name) {
    auto svc = get(name);
    if (!svc) {
        fprintf(stderr, "[ServiceManager] unknown service: %s\n", name.c_str());
        return false;
    }

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

bool ServiceManager::do_stop(const std::string& name) {
    auto svc = get(name);
    if (!svc) {
        fprintf(stderr, "[ServiceManager] unknown service: %s\n", name.c_str());
        return false;
    }

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

bool ServiceManager::start(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return do_start(name);
}

bool ServiceManager::stop(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    return do_stop(name);
}

bool ServiceManager::restart(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto svc = get(name);
    if (!svc) return false;
    return svc->restart();
}

void ServiceManager::start_all() {
    for (const auto& [name, _] : services_) {
        start(name);
    }
}

void ServiceManager::stop_all() {
    auto order = resolve_start_order();
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        stop(*it);
    }
}

void ServiceManager::start_group(const std::vector<std::string>& names) {
    for (const auto& n : names) {
        if (n == "all") { start_all(); }
        else            { start(n); }
    }
}

void ServiceManager::stop_group(const std::vector<std::string>& names) {
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        if (*it == "all") { stop_all(); }
        else              { stop(*it); }
    }
}

std::vector<ServiceStatus> ServiceManager::all_status() const {
    std::vector<ServiceStatus> result;
    for (const auto& [name, svc] : services_) {
        result.push_back(svc->status());
    }
    return result;
}

std::vector<std::string> ServiceManager::resolve_start_order() const {
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
