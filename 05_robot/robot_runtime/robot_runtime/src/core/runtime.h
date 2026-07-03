/**
 * @file runtime.h
 * @brief Runtime 主类，组合所有管控层
 * @role core
 */
#pragma once

#include <string>
#include <memory>
#include <vector>

#include "runtime/process/service_manager/service_manager.h"   // ServiceStatus, ProcessService
#include "runtime/monitor/heartbeat/heartbeat_state.h"          // HeartbeatState

namespace robot_runtime {
class ServiceManager;
class ModeManager;
class MonitorManager;
class DependencyManager;
class HeartbeatMonitor;
class IHeartbeatSource;
class ServiceAccessManager;

namespace net {
struct TcpConfig;
class TcpServer;
}

/**
 * @class Runtime
 * @brief 管控系统组合根，初始化并协调所有管理器
 * @responsibility 服务/模式/监控/心跳/网络全生命周期管理
 */
class Runtime {
public:
    Runtime(std::string workspace_dir,
            std::string config_dir,
            std::string log_dir);
    ~Runtime();

    /**
     * @brief 初始化所有管理器并加载配置
     * @return true=成功
     */
    bool init();

    /**
     * @brief 启动服务
     * @param name 服务名
     * @return true=成功
     */
    bool start_service(const std::string& name);
    bool stop_service(const std::string& name);
    bool restart_service(const std::string& name);
    void start_all();
    void stop_all();

    /**
     * @brief 切换模式
     * @param mode_name 目标模式
     * @return true=成功
     */
    bool switch_mode(const std::string& mode_name);
    void apply_default_mode();

    /**
     * @brief 获取所有服务状态
     * @return 服务状态列表
     */
    std::vector<ServiceStatus> all_status() const;
    std::shared_ptr<ProcessService> get_service(const std::string& name) const;

    ServiceManager& service_manager();
    ModeManager&    mode_manager();
    MonitorManager& monitor_manager();

    HeartbeatMonitor& heartbeat_monitor();
    HeartbeatState GetHeartbeatState(const std::string& name) const;

    /**
     * @brief 获取服务访问管理器
     * @return 服务访问管理器引用
     */
    ServiceAccessManager& service_access();

    bool start_tcp_server(const net::TcpConfig& cfg);
    void serve();  // 阻塞，保持进程常驻

private:
    void load_network_config();
    void init_heartbeat_monitor();

    std::string workspace_dir_;  // 工作目录
    std::string config_dir_;     // 配置文件目录
    std::string log_dir_;        // 日志输出目录

    std::unique_ptr<ServiceManager> sm_;
    std::unique_ptr<ModeManager>    mm_;
    std::unique_ptr<MonitorManager> mon_;
    std::unique_ptr<DependencyManager> dm_;
    std::unique_ptr<HeartbeatMonitor> hb_mon_;
    std::unique_ptr<IHeartbeatSource> heartbeat_source_;
    std::unique_ptr<ServiceAccessManager> svc_access_;
    std::unique_ptr<net::TcpServer> tcp_server_;
};

} // namespace robot_runtime
