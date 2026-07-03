/**
 * @file i_service_client.h
 * @brief 服务能力调用抽象接口
 * @role runtime/service_access
 */
#pragma once

#include <string>

#include "runtime/service_access/service_client.h"

namespace robot_runtime {

struct ServiceStateInfo {
    std::string service;
    std::string state;      // "running", "stopped", "activating", "deactivating"
    bool ready    = false;
    bool active   = false;
    std::string health;     // "healthy", "degraded", "unhealthy"
    std::string message;
};

/// 服务能力调用接口
///
/// 由 adapter 层实现具体协议（ROS2 / AimRT / TCP）。
/// Runtime 核心层只依赖此接口，不依赖具体通信协议。
class IServiceClient {
public:
    virtual ~IServiceClient() = default;

    /// 激活服务（使能力就绪）
    virtual ServiceResult Activate(const std::string& service) = 0;

    /// 停用服务（挂起能力，保持进程）
    virtual ServiceResult Deactivate(const std::string& service) = 0;

    /// 查询服务状态
    virtual ServiceStateInfo GetState(const std::string& service) = 0;

    /// 服务类型标识
    virtual const std::string& type() const = 0;
};

} // namespace robot_runtime
