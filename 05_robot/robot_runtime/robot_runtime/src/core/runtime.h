#pragma once

#include <string>
#include <memory>
#include <vector>

// 值类型数据结构（轻量，必须 include 以支持返回值）
#include "runtime/process/service_manager/service_manager.h"   // ServiceStatus, ProcessService
#include "runtime/monitor/heartbeat/heartbeat_state.h"          // HeartbeatState

// 前向声明 — 所有具体实现类只在 .cpp 中 include
namespace robot_runtime {
class ServiceManager;
class ModeManager;
class MonitorManager;
class DependencyManager;
class HeartbeatMonitor;
class IHeartbeatSource;

namespace net {
struct TcpConfig;
class TcpServer;
} // namespace net

class Runtime {
public:
    Runtime(std::string workspace_dir,
            std::string config_dir,
            std::string log_dir);
    ~Runtime();

    bool init();

    // 服务管控
    bool start_service(const std::string& name);
    bool stop_service(const std::string& name);
    bool restart_service(const std::string& name);
    void start_all();
    void stop_all();

    // 模式管控
    bool switch_mode(const std::string& mode_name);
    void apply_default_mode();

    // 查询
    std::vector<ServiceStatus> all_status() const;
    std::shared_ptr<ProcessService> get_service(const std::string& name) const;

    // 管理器访问（实现在 .cpp）
    ServiceManager& service_manager();
    ModeManager&    mode_manager();
    MonitorManager& monitor_manager();

    // 心跳状态访问
    HeartbeatMonitor& heartbeat_monitor();
    HeartbeatState GetHeartbeatState(const std::string& name) const;

    // 网络服务
    bool start_tcp_server(const net::TcpConfig& cfg);
    void serve();  // 阻塞，保持进程常驻

private:
    void load_network_config();
    void init_heartbeat_monitor();

    std::string workspace_dir_;
    std::string config_dir_;
    std::string log_dir_;

    std::unique_ptr<ServiceManager> sm_;
    std::unique_ptr<ModeManager>    mm_;
    std::unique_ptr<MonitorManager> mon_;
    std::unique_ptr<DependencyManager> dm_;
    std::unique_ptr<HeartbeatMonitor> hb_mon_;
    std::unique_ptr<IHeartbeatSource> heartbeat_source_;
    std::unique_ptr<net::TcpServer> tcp_server_;
};

} // namespace robot_runtime
