#include "adapter/ros2/ros2_heartbeat_source.h"
#include "runtime/monitor/heartbeat/heartbeat_monitor.h"

#include <cstdio>

// ROS2 头文件必须在任何 namespace 之外引入，避免 C++17 + GCC 11 下
// 的 std::sqrt 歧义问题（rclcpp 内部 using namespace std 与 robot_runtime 命名空间冲突）
#ifdef HAS_ROS2
#include <rclcpp/rclcpp.hpp>
#ifdef HAS_ROS2_INTERFACES
#include <robot_runtime_ros2_interfaces/msg/heartbeat.hpp>
#else
#include <std_msgs/msg/string.hpp>
#endif
#endif

namespace robot_runtime {

// ============================================================================
// HAS_ROS2 分支
// ============================================================================
#ifdef HAS_ROS2

class Ros2HeartbeatSource::Impl {
public:
    Impl(HeartbeatMonitor* monitor) : monitor_(monitor) {}

    bool Start(const std::vector<std::string>& topics) {
        (void)topics;
        if (!rclcpp::ok()) {
            fprintf(stderr, "[Ros2HeartbeatSource] ROS2 未初始化\n");
            return false;
        }

        node_ = std::make_shared<rclcpp::Node>("heartbeat_source");
        const std::string hb_topic = "/runtime/heartbeat";

#ifdef HAS_ROS2_INTERFACES
        // 所有服务共用 /runtime/heartbeat 话题，按 msg.service_name 区分
        auto sub = node_->create_subscription<robot_runtime_ros2_interfaces::msg::Heartbeat>(
            hb_topic, 10,
            [this](const robot_runtime_ros2_interfaces::msg::Heartbeat::SharedPtr msg) {
                if (!monitor_ || msg->service_name.empty()) return;
                monitor_->OnHeartbeat({msg->service_name, msg->timestamp});
            });
        subs_.push_back(sub);
        printf("[Ros2HeartbeatSource] 订阅: %s (Heartbeat.msg)\n", hb_topic.c_str());
#else
        // 降级：按旧格式 topic/service 订阅 std_msgs::String
        for (const auto& entry : topics) {
            auto sep = entry.find('/');
            if (sep == std::string::npos || sep == 0 || sep == entry.size() - 1) {
                fprintf(stderr, "[Ros2HeartbeatSource] 无效格式: %s\n", entry.c_str());
                continue;
            }
            std::string topic_name = entry.substr(0, sep);
            std::string service_name = entry.substr(sep + 1);

            auto sub = node_->create_subscription<std_msgs::msg::String>(
                topic_name, 10,
                [this, service_name](const std_msgs::msg::String::SharedPtr msg) {
                    (void)msg;
                    if (!monitor_) return;
                    struct timespec ts;
                    clock_gettime(CLOCK_REALTIME, &ts);
                    double now = static_cast<double>(ts.tv_sec)
                               + static_cast<double>(ts.tv_nsec) / 1e9;
                    monitor_->OnHeartbeat({service_name, now});
                });
            subs_.push_back(sub);
            printf("[Ros2HeartbeatSource] 订阅: %s → %s (std_msgs::String, fallback)\n",
                   topic_name.c_str(), service_name.c_str());
        }
#endif

        spin_thread_ = std::thread([this]() {
            while (spinning_) {
                rclcpp::spin_some(node_);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        return true;
    }

    void Stop() {
        spinning_ = false;
        if (spin_thread_.joinable()) spin_thread_.join();
        subs_.clear();
        node_.reset();
    }

private:
    HeartbeatMonitor* monitor_ = nullptr;
    rclcpp::Node::SharedPtr node_;
    std::vector<rclcpp::SubscriptionBase::SharedPtr> subs_;
    std::thread spin_thread_;
    std::atomic<bool> spinning_{true};
};

#else  // !HAS_ROS2

class Ros2HeartbeatSource::Impl {
public:
    Impl(HeartbeatMonitor* monitor) : monitor_(monitor) {
        fprintf(stderr, "[Ros2HeartbeatSource] ROS2 未启用 (编译时未找到 rclcpp)\n");
    }

    bool Start(const std::vector<std::string>& topics) {
        (void)topics;
        fprintf(stderr, "[Ros2HeartbeatSource] ROS2 未启用，无法订阅心跳话题\n");
        return false;
    }

    void Stop() {}

private:
    HeartbeatMonitor* monitor_ = nullptr;
};

#endif  // HAS_ROS2

// ============================================================================
// Ros2HeartbeatSource 公开接口
// ============================================================================

Ros2HeartbeatSource::Ros2HeartbeatSource(HeartbeatMonitor* monitor)
    : monitor_(monitor)
    , impl_(std::make_unique<Impl>(monitor))
{}

Ros2HeartbeatSource::~Ros2HeartbeatSource() { Stop(); }

bool Ros2HeartbeatSource::Start(const std::vector<std::string>& topics) {
    if (running_) return true;
    if (!impl_->Start(topics)) return false;
    running_ = true;
    return true;
}

void Ros2HeartbeatSource::Stop() {
    if (!running_) return;
    impl_->Stop();
    running_ = false;
}

} // namespace robot_runtime
