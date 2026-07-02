#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "runtime/service_access/service_registry.h"
#include "runtime/service_access/service_proxy.h"
#include "runtime/service_access/i_service_client.h"

namespace robot_runtime {

/// 服务访问管理器
///
/// 统一管理 runtime 对外部服务的访问上下文：
///   - 服务能力调用 (Activate/Deactivate/GetState)
///   - 服务注册与发现
///   - 代理创建与销毁
///
/// 不直接依赖具体通信协议；根据服务 type 自动选择对应的 IServiceClient 实现。
class ServiceAccessManager {
public:
    ServiceAccessManager();
    ~ServiceAccessManager();

    // ---- 能力调用（适配器转发） ----

    /// 激活服务能力（通过对应协议的 IServiceClient 调用）
    ServiceResult Activate(const std::string& service_name);

    /// 停用服务能力
    ServiceResult Deactivate(const std::string& service_name);

    /// 查询服务状态
    ServiceStateInfo GetState(const std::string& service_name);

    // ---- 客户端注册 ----

    /// 注册一个协议适配器（如 Ros2ServiceClient）
    void register_client(const std::string& protocol_type,
                         std::unique_ptr<IServiceClient> client);

    /// 根据服务名获取对应协议的客户端
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
