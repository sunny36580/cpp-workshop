#include "runtime/service_access/service_access_manager.h"

namespace robot_runtime {

ServiceAccessManager::ServiceAccessManager() = default;
ServiceAccessManager::~ServiceAccessManager() { close_all(); }

std::shared_ptr<ServiceProxy> ServiceAccessManager::get_proxy(const std::string& service_name) {
    auto it = proxies_.find(service_name);
    if (it != proxies_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ServiceAccessManager::add_proxy(std::shared_ptr<ServiceProxy> proxy) {
    if (!proxy) return false;
    const auto& name = proxy->service_name();
    if (proxies_.count(name)) return false;  // 已存在
    proxies_[name] = std::move(proxy);
    return true;
}

void ServiceAccessManager::remove_proxy(const std::string& service_name) {
    auto it = proxies_.find(service_name);
    if (it != proxies_.end()) {
        it->second->close();
        proxies_.erase(it);
    }
}

void ServiceAccessManager::create_proxies(const std::string& protocol_filter) {
    for (const auto* entry : registry_.all()) {
        if (proxies_.count(entry->service_name)) continue;
        if (entry->capabilities.empty()) continue;

        // 如果指定了协议过滤，跳过不匹配的
        if (!protocol_filter.empty()) {
            bool match = false;
            for (const auto& cap : entry->capabilities) {
                if (cap.protocol == protocol_filter) {
                    match = true;
                    break;
                }
            }
            if (!match) continue;
        }

        // 代理由外部 adapter factory 创建后通过 add_proxy 注入
        // 这里只做注册表一致性检查
        registry_.update_alive(entry->service_name, false);
    }
}

void ServiceAccessManager::close_all() {
    for (auto& [name, proxy] : proxies_) {
        proxy->close();
    }
    proxies_.clear();
}

} // namespace robot_runtime
