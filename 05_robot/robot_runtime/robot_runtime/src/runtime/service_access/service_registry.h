/**
 * @file service_registry.h
 * @brief 服务注册表
 * @role runtime/service_access
 */
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

/**
 * @class ServiceRegistry
 * @brief 服务注册表
 * @responsibility 管理所有可访问的服务及其能力描述
 */
class ServiceRegistry {
public:
    using EntryCallback = std::function<void(const ServiceEntry&)>;

    /** @brief 注册一个服务条目 */
    void register_service(const ServiceEntry& entry);
    void register_service(ServiceEntry&& entry);

    /** @brief 注销服务 */
    void unregister_service(const std::string& service_name);

    /**
     * @brief 按名称查找服务
     * @return 找到返回指针，未找到返回 nullptr
     */
    const ServiceEntry* find(const std::string& service_name) const;

    /**
     * @brief 按能力查询服务
     * @param cap_name 能力名称
     * @return 匹配的服务列表
     */
    std::vector<const ServiceEntry*> find_by_capability(const std::string& cap_name) const;

    /**
     * @brief 按协议类型查询
     * @param protocol 协议类型
     * @return 匹配的服务列表
     */
    std::vector<const ServiceEntry*> find_by_protocol(const std::string& protocol) const;

    /**
     * @brief 列出全部已注册服务
     * @return 全部服务列表
     */
    std::vector<const ServiceEntry*> all() const;

    /**
     * @brief 更新服务在线状态
     * @param service_name 服务名
     * @param alive 是否在线
     */
    void update_alive(const std::string& service_name, bool alive);

    void set_on_registered(EntryCallback cb)   { on_registered_ = std::move(cb); }
    void set_on_unregistered(EntryCallback cb) { on_unregistered_ = std::move(cb); }
    void clear();

private:
    std::unordered_map<std::string, ServiceEntry> entries_;
    EntryCallback on_registered_;
    EntryCallback on_unregistered_;
};

} // namespace robot_runtime
