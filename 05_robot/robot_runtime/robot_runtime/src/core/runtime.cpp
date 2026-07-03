#include "core/runtime.h"
#include "adapter/ros2/ros2_heartbeat_source.h"
#include "adapter/ros2/ros2_service_client.h"
#include "orchestration/mode/mode_manager.h"
#include "runtime/monitor/monitor_manager.h"
#include "runtime/monitor/heartbeat/heartbeat_monitor.h"
#include "runtime/monitor/heartbeat/i_heartbeat_source.h"
#include "runtime/process/dependency_manager/dependency_manager.h"
#include "runtime/service_access/service_access_manager.h"
#include "gateway/tcp/tcp_server.h"

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

/**
 * @brief 初始化所有管理器并加载配置
 * @return true=初始化成功
 */
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

    // 初始化服务访问管理器 + 注册协议适配器
    svc_access_ = std::make_unique<ServiceAccessManager>();
    {
        // 将所有已加载的服务注册到 registry（适配器用 type 字段选择协议）
        for (const auto& [name, svc] : sm_->services()) {
            ServiceEntry entry;
            entry.service_name = name;
            entry.service_type = svc->config().type;
            entry.alive = false;
            ServiceCapability cap;
            cap.name = "lifecycle";
            cap.protocol = svc->config().type;
            entry.capabilities.push_back(std::move(cap));
            svc_access_->registry().register_service(std::move(entry));
        }
#ifdef HAS_ROS2
        svc_access_->register_client("ros2", std::make_unique<Ros2ServiceClient>());
        printf("[Runtime] registered ROS2 service client\n");
#else
        printf("[Runtime] ROS2 not available, service clients disabled\n");
#endif
    }

    // 加载监控配置（含文件心跳检测）
    mon_->load_config(config_dir_ + "/monitor.yaml");
    mon_->start();

    // 初始化心跳监控（纯内存状态机，无文件/ROS 依赖）
    init_heartbeat_monitor();

    // 应用默认模式（按 modes.yaml 启动对应服务）
    mm_->apply_default();

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

/**
 * @brief 启动 TCP 远程管控服务
 * @param cfg TCP 配置
 * @return true=启动成功
 */
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

ServiceManager&      Runtime::service_manager()    { return *sm_; }
ModeManager&         Runtime::mode_manager()       { return *mm_; }
MonitorManager&      Runtime::monitor_manager()    { return *mon_; }
HeartbeatMonitor&    Runtime::heartbeat_monitor()  { return *hb_mon_; }
ServiceAccessManager& Runtime::service_access()    { return *svc_access_; }

// =====================================================================
// 服务 / 模式管控（转发到各 Manager）
// =====================================================================
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

// =====================================================================
// 心跳监控初始化
// =====================================================================
void Runtime::init_heartbeat_monitor() {
    hb_mon_ = std::make_unique<HeartbeatMonitor>();

    // 尝试从 runtime.yaml 加载心跳配置
    std::string hb_cfg = config_dir_ + "/runtime.yaml";
    if (fs::exists(hb_cfg)) {
        try {
            auto root = YAML::LoadFile(hb_cfg);
            auto hb = root["heartbeat"];
            if (hb) {
                double timeout = hb["timeout_sec"].as<double>(8.0);
                hb_mon_->SetDefaultTimeoutSec(timeout);

                if (hb["targets"]) {
                    std::vector<HeartbeatTarget> targets;
                    for (const auto& entry : hb["targets"]) {
                        HeartbeatTarget t;
                        t.name        = entry["name"].as<std::string>();
                        t.timeout_sec = entry["timeout_sec"] ? entry["timeout_sec"].as<double>() : timeout;
                        targets.push_back(t);
                    }
                    hb_mon_->AddTargets(targets);
                }
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[Runtime] 加载心跳配置失败: %s\n", e.what());
        }
    }

    // 至少为每个已注册服务添加心跳监控（默认超时）
    for (const auto& [name, svc] : sm_->services()) {
        HeartbeatTarget t;
        t.name = name;
        t.timeout_sec = 8.0;
        hb_mon_->AddTarget(t);
    }

    hb_mon_->Start();
    printf("[Runtime] heartbeat monitor started (%zu targets)\n",
           hb_mon_->GetAllStates().size());

    // ---- ROS2 心跳话题订阅源（可选） ----
    auto mon_cfg = config_dir_ + "/monitor.yaml";
    if (fs::exists(mon_cfg)) {
        try {
            auto root = YAML::LoadFile(mon_cfg);
            auto ros2_hb = root["monitor"]["ros2_heartbeat"];
            if (ros2_hb && ros2_hb["enabled"].as<bool>(false)) {
                std::vector<std::string> topics;
                if (ros2_hb["topic_mapping"]) {
                    for (const auto& entry : ros2_hb["topic_mapping"]) {
                        auto topic   = entry["topic"].as<std::string>();
                        auto service = entry["service"].as<std::string>();
                        topics.push_back(topic + "/" + service);
                    }
                }
                if (!topics.empty()) {
                    // Ros2HeartbeatSource 是 IHeartbeatSource 的实现
                    heartbeat_source_ = std::make_unique<Ros2HeartbeatSource>(hb_mon_.get());
                    if (heartbeat_source_->Start(topics)) {
                        printf("[Runtime] ROS2 heartbeat source started (%zu topics)\n",
                               topics.size());
                    }
                }
            }
        } catch (const std::exception& e) {
            fprintf(stderr, "[Runtime] 加载 ROS2 心跳配置失败: %s\n", e.what());
        }
    }
}

/**

 * @return 指定服务心跳状态

 */
HeartbeatState Runtime::GetHeartbeatState(const std::string& name) const {
    if (hb_mon_) return hb_mon_->GetState(name);
    return {name, HeartbeatStatus::Offline, 0.0, 8.0};
}

} // namespace robot_runtime
