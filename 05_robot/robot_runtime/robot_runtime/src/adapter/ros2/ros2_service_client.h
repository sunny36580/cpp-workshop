#pragma once

#include <memory>
#include <string>

#include "runtime/service_access/i_service_client.h"

namespace robot_runtime {

/// ROS2 服务能力调用客户端
///
/// 实现 IServiceClient 接口，通过 ROS2 service 调用目标服务的生命周期接口。
///
/// 默认推导 ROS2 service 名称：
///   Activate(service)   → /{service}/lifecycle (LifecycleCommand.srv)
///   Deactivate(service) → /{service}/lifecycle
///   GetState(service)   → /{service}/get_state  (GetServiceState.srv)
///
/// 编译条件：
///   - HAS_ROS2=1：实际调用 ROS2 service
///   - 否则：桩实现，仅日志提示
class Ros2ServiceClient : public IServiceClient {
public:
    Ros2ServiceClient();
    ~Ros2ServiceClient() override;

    ServiceResult Activate(const std::string& service) override;
    ServiceResult Deactivate(const std::string& service) override;
    ServiceStateInfo GetState(const std::string& service) override;
    const std::string& type() const override { static std::string t = "ros2"; return t; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace robot_runtime
