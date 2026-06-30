#pragma once

#include <string>
#include <memory>
#include <unordered_map>

#include "runtime/service_access/service_registry.h"
#include "runtime/service_access/service_proxy.h"

namespace robot_runtime {

/// 服务访问管理器
/// 统一管理 runtime 对外部服务的访问上下文：
///   - 服务注册与发现
///   - 代理创建与销毁
///   - 连接生命周期
///
/// 不直接依赖具体通信协议，通过 adapter 层注入 Factory 来创建客户端。
class ServiceAccessManager {
public:
    ServiceAccessManager();
    ~ServiceAccessManager();

    /// 获取注册表（可读写）
    ServiceRegistry& registry() { return registry_; }
    const ServiceRegistry& registry() const { return registry_; }

    /// 创建或获取服务代理
    /// 如果已存在同名代理，直接返回
    std::shared_ptr<ServiceProxy> get_proxy(const std::string& service_name);

    /// 注册代理（由 adapter factory 创建后注入）
    bool add_proxy(std::shared_ptr<ServiceProxy> proxy);

    /// 移除代理
    void remove_proxy(const std::string& service_name);

    /// 批量创建代理（从 registry 中遍历，匹配指定协议）
    /// protocol_filter 为空时创建所有已注册服务的代理
    void create_proxies(const std::string& protocol_filter = "");

    /// 关闭所有代理
    void close_all();

    /// 代理数量
    size_t proxy_count() const { return proxies_.size(); }

private:
    ServiceRegistry registry_;
    std::unordered_map<std::string, std::shared_ptr<ServiceProxy>> proxies_;
};

} // namespace robot_runtime
