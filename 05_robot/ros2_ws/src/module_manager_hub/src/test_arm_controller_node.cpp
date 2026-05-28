#include <rclcpp/rclcpp.hpp>
#include "module_manager_hub/msg/robotarmcontrol.hpp"
#include "module_manager_hub/msg/robotarmstatus.hpp"

class TestArmControllerNode : public rclcpp::Node
{
public:
  TestArmControllerNode() : Node("test_arm_controller_node")
  {
    // 订阅机械臂控制指令
    control_sub_ = this->create_subscription<module_manager_hub::msg::Robotarmcontrol>(
      "/arm/control", 10,
      [this](const module_manager_hub::msg::Robotarmcontrol::SharedPtr msg) {
        RCLCPP_INFO(this->get_logger(), "收到机械臂控制指令: %zu 个电机", msg->motor_ids.size());
        // 模拟响应：更新状态
        module_manager_hub::msg::Robotarmstatus status_msg;
        status_msg.motor_ids = msg->motor_ids;
        status_msg.actual_positions = msg->target_positions;
        status_msg.actual_velocities = msg->target_velocitie;
        status_pub_->publish(status_msg);
      }
    );

    // 定期发布状态（模拟心跳）
    status_pub_ = this->create_publisher<module_manager_hub::msg::Robotarmstatus>("/arm/status", 10);
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(500),
      [this]() {
        module_manager_hub::msg::Robotarmstatus msg;
        // 简单模拟数据
        msg.motor_ids = {1, 2, 3};
        msg.actual_positions = {0.0, 0.0, 0.0};
        msg.actual_velocities = {0.0, 0.0, 0.0};
        status_pub_->publish(msg);
        RCLCPP_DEBUG(this->get_logger(), "发布机械臂状态");
      }
    );

    RCLCPP_INFO(this->get_logger(), "机械臂控制测试节点启动");
  }

private:
  rclcpp::Subscription<module_manager_hub::msg::Robotarmcontrol>::SharedPtr control_sub_;
  rclcpp::Publisher<module_manager_hub::msg::Robotarmstatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<TestArmControllerNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
