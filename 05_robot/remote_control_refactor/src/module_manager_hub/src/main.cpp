#include "module_manager_hub/ros/serial_joy_bridge_node.h"
#include "module_manager_hub/ros/camera_streamer_node.h"

#include <robot_runtime_ros2_interfaces/srv/lifecycle_command.hpp>
#include <robot_runtime_ros2_interfaces/srv/get_service_state.hpp>
#include <robot_runtime_ros2_interfaces/msg/heartbeat.hpp>

#include <rclcpp/rclcpp.hpp>
#include <signal.h>
#include <memory>

using namespace std::chrono_literals;

/// module_manager_hub 进程服务节点
/// 内联实现生命周期 + 心跳 + 状态查询，不依赖 SDK
class ModuleManagerHubService : public rclcpp::Node
{
public:
  ModuleManagerHubService()
    : Node("module_manager_hub")
  {
    joy_ = std::make_shared<SerialJoyBridgeNode>("serial_joy_bridge");
    cam_ = std::make_shared<CameraStreamerNode>("camera_streamer_node");

    // 生命周期服务
    lifecycle_srv_ = this->create_service<robot_runtime_ros2_interfaces::srv::LifecycleCommand>(
        "/module_manager_hub/lifecycle",
        std::bind(&ModuleManagerHubService::handleLifecycle, this,
                  std::placeholders::_1, std::placeholders::_2));

    // 状态查询服务
    get_state_srv_ = this->create_service<robot_runtime_ros2_interfaces::srv::GetServiceState>(
        "/module_manager_hub/get_state",
        std::bind(&ModuleManagerHubService::handleGetState, this,
                  std::placeholders::_1, std::placeholders::_2));

    // 1Hz 心跳
    hb_pub_ = this->create_publisher<robot_runtime_ros2_interfaces::msg::Heartbeat>(
        "/runtime/heartbeat", 10);
    hb_timer_ = this->create_wall_timer(1s, std::bind(&ModuleManagerHubService::publishHeartbeat, this));

    RCLCPP_INFO(this->get_logger(), "module_manager_hub 服务就绪");
  }

  std::shared_ptr<SerialJoyBridgeNode> joy() const { return joy_; }
  std::shared_ptr<CameraStreamerNode> cam() const { return cam_; }

private:
  void handleLifecycle(
      const std::shared_ptr<robot_runtime_ros2_interfaces::srv::LifecycleCommand::Request> req,
      std::shared_ptr<robot_runtime_ros2_interfaces::srv::LifecycleCommand::Response> res)
  {
    if (req->service_name != "module_manager_hub") {
      res->success = false; res->message = "服务名不匹配"; return;
    }
    if (req->command == "activate") {
      joy_->setActive(true);
      cam_->setActive(true);
      active_ = true;
      res->success = true; res->message = "已激活";
    } else if (req->command == "deactivate") {
      cam_->setActive(false);
      joy_->setActive(false);
      active_ = false;
      res->success = true; res->message = "已停用";
    } else {
      res->success = false; res->message = "未知指令: " + req->command;
    }
  }

  void handleGetState(
      const std::shared_ptr<robot_runtime_ros2_interfaces::srv::GetServiceState::Request>,
      std::shared_ptr<robot_runtime_ros2_interfaces::srv::GetServiceState::Response> res)
  {
    res->service_name = "module_manager_hub";
    res->state = active_ ? "running" : "stopped";
    res->ready = true;
    res->active = active_;
    res->health = "healthy";
  }

  void publishHeartbeat()
  {
    robot_runtime_ros2_interfaces::msg::Heartbeat hb;
    hb.service_name = "module_manager_hub";
    hb.timestamp = this->now().seconds();
    hb.state = active_ ? "online" : "inactive";
    hb_pub_->publish(hb);
  }

  std::shared_ptr<SerialJoyBridgeNode> joy_;
  std::shared_ptr<CameraStreamerNode> cam_;
  bool active_ = false;

  rclcpp::Service<robot_runtime_ros2_interfaces::srv::LifecycleCommand>::SharedPtr lifecycle_srv_;
  rclcpp::Service<robot_runtime_ros2_interfaces::srv::GetServiceState>::SharedPtr get_state_srv_;
  rclcpp::Publisher<robot_runtime_ros2_interfaces::msg::Heartbeat>::SharedPtr hb_pub_;
  rclcpp::TimerBase::SharedPtr hb_timer_;
};

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  signal(SIGPIPE, SIG_IGN);

  auto service = std::make_shared<ModuleManagerHubService>();

  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(service);
  exec.add_node(service->joy());
  exec.add_node(service->cam());
  exec.spin();

  rclcpp::shutdown();
  return 0;
}