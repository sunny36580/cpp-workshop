/**
 * @file service_access_manager.h
 * @brief 服务访问管理器
 * @role runtime/service_access
 */
#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "runtime/service_access/service_registry.h"
#include "runtime/service_access/service_proxy.h"
#include "runtime/service_access/i_service_client.h"

namespace robot_runtime {

/**
 * @class ServiceAccessManager
 * @brief 服务访问管理器
 * @responsibility 根据服务 type 选择适配器，转发 Activate/Deactivate/GetState
 */
class ServiceAccessManager {
public:
    ServiceAccessManager();
    ~ServiceAccessManager();

    /**
     * @brief 激活服务能力
     * @param service_name 服务名
     * @return 调用结果
     */
    ServiceResult Activate(const std::string& service_name);

    /**
     * @brief 停用服务能力
     * @param service_name 服务名
     * @return 调用结果
     */
    ServiceResult Deactivate(const std::string& service_name);

    /**
     * @brief 查询服务状态
     * @param service_name 服务名
     * @return 状态信息
     */
    ServiceStateInfo GetState(const std::string& service_name);

    /**
     * @brief 注册协议适配器
     * @param protocol_type 协议类型(ros2/tcp/aimrt)
     * @param client 适配器实例
     */
    void register_client(const std::string& protocol_type,
                         std::unique_ptr<IServiceClient> client);

    /**
     * @brief 根据服务名获取对应协议的客户端
     * @param service_name 服务名
     * @return 客户端指针，未找到返回 nullptr
     */
    IServiceClient* get_client(const std::string& service_name) const;

    // ---- 代理管理 ----

    ServiceRegistry& registry() { return registry_; }
    const ServiceRegistry& registry() const { return registry_; }

    std::shared_ptr<ServiceProxy> get_proxy(const std::string& service_name);
    bool add_proxy(std::shared_ptr<ServiceProxy> proxy);
    void remove_proxy(const std::string& service_name);
    void create_proxies(const std::string& protocol_filter = "");
    void close_all();
    size_t proxy_count() const { return proxies_.size(); }

private:
    ServiceRegistry registry_;
    std::unordered_map<std::string, std::shared_ptr<ServiceProxy>> proxies_;
    std::unordered_map<std::string, std::unique_ptr<IServiceClient>> clients_;
};

} // namespace robot_runtime
