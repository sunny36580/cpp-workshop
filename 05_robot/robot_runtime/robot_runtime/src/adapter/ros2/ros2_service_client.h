/**
 * @file ros2_service_client.h
 * @brief ROS2 服务能力调用适配器
 * @role adapter/ros2
 */
#pragma once

#include <memory>
#include <string>

#include "runtime/service_access/i_service_client.h"

namespace robot_runtime {

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
