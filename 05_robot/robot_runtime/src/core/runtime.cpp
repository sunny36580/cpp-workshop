#include "core/runtime.h"
#include "network/tcp_server.h"

#include <cstdio>
#include <filesystem>
#include <thread>
#include <chrono>
#include <yaml-cpp/yaml.h>

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
    if (tcp_server_) tcp_server_->stop();
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

    // 自动加载网络配置并启动 TCP 服务
    load_network_config();
    return true;
}

void Runtime::load_network_config() {
    std::string net_cfg_path = config_dir_ + "/network.yaml";
    if (!fs::exists(net_cfg_path)) {
        printf("[Runtime] no network.yaml, TCP server disabled\n");
        return;
    }

    try {
        auto root = YAML::LoadFile(net_cfg_path);
        auto net = root["network"];
        if (!net || !net["enabled"].as<bool>(false)) {
            printf("[Runtime] network enabled=false, TCP server disabled\n");
            return;
        }

        net::TcpConfig cfg;
        cfg.port       = net["port"].as<uint16_t>(9527);
        cfg.bind_addr  = net["bind_addr"].as<std::string>("0.0.0.0");
        cfg.auth_token = net["auth_token"].as<std::string>("");
        cfg.timeout_ms = net["timeout_ms"].as<int>(5000);
        cfg.max_clients= net["max_clients"].as<int>(4);

        start_tcp_server(cfg);
    } catch (const std::exception& e) {
        fprintf(stderr, "[Runtime] failed to load network.yaml: %s\n", e.what());
    }
}

bool Runtime::start_tcp_server(const net::TcpConfig& cfg) {
    tcp_server_ = std::make_unique<net::TcpServer>(*this, cfg);
    if (!tcp_server_->start()) {
        fprintf(stderr, "[Runtime] failed to start TCP server\n");
        tcp_server_.reset();
        return false;
    }
    return true;
}

void Runtime::serve() {
    if (tcp_server_) {
        printf("[Runtime] entering serve loop (Ctrl+C to exit)\n");
        tcp_server_->serve();
    } else {
        printf("[Runtime] no TCP server, blocking on monitor...\n");
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
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
