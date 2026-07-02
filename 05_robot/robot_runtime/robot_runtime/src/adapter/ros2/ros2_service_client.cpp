#include "adapter/ros2/ros2_service_client.h"

#include <cstdio>
#include <mutex>
#include <unordered_map>

// ROS2 头文件必须在 namespace 外引入（避免 C++17 + GCC 11 下的命名空间冲突）
#ifdef HAS_ROS2
#include <rclcpp/node.hpp>
#include <rclcpp/client.hpp>
#endif

#ifdef HAS_ROS2_INTERFACES
#include <robot_runtime_ros2_interfaces/srv/lifecycle_command.hpp>
#include <robot_runtime_ros2_interfaces/srv/get_service_state.hpp>
#endif

namespace robot_runtime {

// ============================================================================
// HAS_ROS2 分支 — 真实 ROS2 service 调用
// ============================================================================

#ifdef HAS_ROS2

class Ros2ServiceClient::Impl {
public:
    Impl() {
        if (!rclcpp::ok()) {
            fprintf(stderr, "[Ros2ServiceClient] ROS2 未初始化\n");
            return;
        }
        node_ = std::make_shared<rclcpp::Node>("runtime_service_client");
        fprintf(stderr, "[Ros2ServiceClient] ROS2 client ready\n");
    }

private:
    /// 懒加载缓存 client（相同 service+suffix 复用）
    template<typename Srv>
    typename rclcpp::Client<Srv>::SharedPtr
    get_cached_client(const std::string& service, const std::string& suffix) {
        const std::string key = "/" + service + suffix;
        {
            std::lock_guard<std::mutex> lock(clients_mutex_);
            auto it = clients_.find(key);
            if (it != clients_.end()) {
                return std::dynamic_pointer_cast<rclcpp::Client<Srv>>(it->second);
            }
        }
        auto client = node_->create_client<Srv>(key);
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_[key] = client;
        return client;
    }

    /// 等待服务端就绪（短超时，调用方负责重试）
    static constexpr auto kServiceTimeout = std::chrono::milliseconds(200);

public:
    ServiceResult Activate(const std::string& service) {
        if (!node_) return ServiceResult::Error("ROS2 not initialized");
#ifdef HAS_ROS2_INTERFACES
        using Srv = robot_runtime_ros2_interfaces::srv::LifecycleCommand;
        auto client = get_cached_client<Srv>(service, "/lifecycle");
        if (!client) return ServiceResult::Error("create lifecycle client failed");
        if (!client->wait_for_service(kServiceTimeout)) {
            return ServiceResult::Error("lifecycle service not available: " + service);
        }
        auto req = std::make_shared<Srv::Request>();
        req->command = "activate";
        auto future = client->async_send_request(req);
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            return ServiceResult::Error("Activate timeout");
        }
        auto resp = future.get();
        return resp->success ? ServiceResult::Ok() : ServiceResult::Error(resp->message);
#else
        fprintf(stderr, "[Ros2ServiceClient] Activate (HAS_ROS2_INTERFACES not set)\n");
        return ServiceResult::Ok();
#endif
    }

    ServiceResult Deactivate(const std::string& service) {
        if (!node_) return ServiceResult::Error("ROS2 not initialized");
#ifdef HAS_ROS2_INTERFACES
        using Srv = robot_runtime_ros2_interfaces::srv::LifecycleCommand;
        auto client = get_cached_client<Srv>(service, "/lifecycle");
        if (!client) return ServiceResult::Error("create lifecycle client failed");
        if (!client->wait_for_service(kServiceTimeout)) {
            return ServiceResult::Error("lifecycle service not available: " + service);
        }
        auto req = std::make_shared<Srv::Request>();
        req->command = "deactivate";
        auto future = client->async_send_request(req);
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
            return ServiceResult::Error("Deactivate timeout");
        }
        auto resp = future.get();
        return resp->success ? ServiceResult::Ok() : ServiceResult::Error(resp->message);
#else
        fprintf(stderr, "[Ros2ServiceClient] Deactivate (HAS_ROS2_INTERFACES not set)\n");
        return ServiceResult::Ok();
#endif
    }

    ServiceStateInfo GetState(const std::string& service) {
        ServiceStateInfo info;
        info.service = service;
        if (!node_) {
            info.state = "unknown"; info.health = "unhealthy";
            return info;
        }
#ifdef HAS_ROS2_INTERFACES
        using Srv = robot_runtime_ros2_interfaces::srv::GetServiceState;
        auto client = get_cached_client<Srv>(service, "/get_state");
        if (!client) {
            info.state = "unknown"; info.health = "unhealthy";
            info.message = "create get_state client failed";
            return info;
        }
        if (!client->wait_for_service(kServiceTimeout)) {
            info.state = "unknown"; info.health = "unhealthy";
            info.message = "get_state not available";
            return info;
        }
        auto req = std::make_shared<Srv::Request>();
        auto future = client->async_send_request(req);
        if (future.wait_for(std::chrono::seconds(3)) != std::future_status::ready) {
            info.state = "unknown"; info.health = "unhealthy";
            info.message = "GetState timeout";
            return info;
        }
        auto resp = future.get();
        info.state = resp->state; info.ready = resp->ready;
        info.active = resp->active; info.health = resp->health;
        info.message = resp->message;
        return info;
#else
        fprintf(stderr, "[Ros2ServiceClient] GetState (HAS_ROS2_INTERFACES not set)\n");
        info.state = "running"; info.ready = true;
        info.active = true; info.health = "healthy";
        return info;
#endif
    }

private:
    rclcpp::Node::SharedPtr node_;
    std::unordered_map<std::string, rclcpp::ClientBase::SharedPtr> clients_;
    std::mutex clients_mutex_;
};

#else  // !HAS_ROS2

class Ros2ServiceClient::Impl {
public:
    Impl() {
        fprintf(stderr, "[Ros2ServiceClient] ROS2 未启用 (编译时未找到 rclcpp)\n");
    }
    ServiceResult Activate(const std::string& service) {
        fprintf(stderr, "[Ros2ServiceClient] ROS2 未启用，无法 Activate %s\n", service.c_str());
        return ServiceResult::Error("ROS2 not available");
    }
    ServiceResult Deactivate(const std::string& service) {
        fprintf(stderr, "[Ros2ServiceClient] ROS2 未启用，无法 Deactivate %s\n", service.c_str());
        return ServiceResult::Error("ROS2 not available");
    }
    ServiceStateInfo GetState(const std::string& service) {
        ServiceStateInfo info;
        info.service = service;
        info.state = "unknown";
        info.health = "unhealthy";
        info.message = "ROS2 not available";
        return info;
    }
};

#endif  // HAS_ROS2

// ============================================================================
// Ros2ServiceClient 公开接口
// ============================================================================

Ros2ServiceClient::Ros2ServiceClient()
    : impl_(std::make_unique<Impl>())
{}

Ros2ServiceClient::~Ros2ServiceClient() = default;

ServiceResult Ros2ServiceClient::Activate(const std::string& service) {
    return impl_->Activate(service);
}

ServiceResult Ros2ServiceClient::Deactivate(const std::string& service) {
    return impl_->Deactivate(service);
}

ServiceStateInfo Ros2ServiceClient::GetState(const std::string& service) {
    return impl_->GetState(service);
}

} // namespace robot_runtime
