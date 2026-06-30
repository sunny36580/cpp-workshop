#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace robot_runtime {

/// 服务句柄
/// 轻量级句柄，表示一个已连接/已解析的服务实例。
/// 不拥有连接生命周期，由 ServiceAccessManager 统一管理。
class ServiceHandle {
public:
    explicit ServiceHandle(std::string service_name, uint64_t session_id)
        : service_name_(std::move(service_name))
        , session_id_(session_id) {}

    const std::string& service_name() const { return service_name_; }
    uint64_t session_id() const { return session_id_; }
    bool valid() const { return session_id_ != 0; }

    /// 释放句柄（不关闭连接，只是标记失效）
    void invalidate() { session_id_ = 0; }

private:
    std::string service_name_;
    uint64_t session_id_ = 0;
};

} // namespace robot_runtime
