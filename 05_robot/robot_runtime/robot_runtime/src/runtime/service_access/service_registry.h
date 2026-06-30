#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>

namespace robot_runtime {

/// 服务能力描述
struct ServiceCapability {
    std::string name;         // 能力名称，如 "move", "speak", "heartbeat"
    std::string protocol;     // 通信协议: "ros2", "tcp", "aimrt", "python"
    std::string endpoint;     // 端点标识: topic / port / service name
    std::string msg_type;     // 消息类型
};

/// 服务注册条目
struct ServiceEntry {
    std::string service_name;          // 服务实例名
    std::string service_type;          // 服务类型标识
    std::vector<ServiceCapability> capabilities;  // 提供的能力列表
    bool alive = false;                // 是否在线
};

/// 服务注册表
/// 集中管理所有可访问的服务及其能力描述。
/// 由 ServiceAccessManager 初始化，支持动态注册/发现。
class ServiceRegistry {
public:
    using EntryCallback = std::function<void(const ServiceEntry&)>;

    /// 注册一个服务条目
    void register_service(const ServiceEntry& entry);
    void register_service(ServiceEntry&& entry);

    /// 注销服务
    void unregister_service(const std::string& service_name);

    /// 按名称查找服务
    const ServiceEntry* find(const std::string& service_name) const;

    /// 按能力查询服务
    std::vector<const ServiceEntry*> find_by_capability(const std::string& cap_name) const;

    /// 按协议类型查询
    std::vector<const ServiceEntry*> find_by_protocol(const std::string& protocol) const;

    /// 列出全部已注册服务
    std::vector<const ServiceEntry*> all() const;

    /// 更新服务在线状态
    void update_alive(const std::string& service_name, bool alive);

    /// 注册/注销监听
    void set_on_registered(EntryCallback cb)   { on_registered_ = std::move(cb); }
    void set_on_unregistered(EntryCallback cb) { on_unregistered_ = std::move(cb); }

    /// 清空
    void clear();

private:
    std::unordered_map<std::string, ServiceEntry> entries_;
    EntryCallback on_registered_;
    EntryCallback on_unregistered_;
};

} // namespace robot_runtime
