#include "module_manager_hub/serial_joy_bridge.h"
#include <rclcpp/rclcpp.hpp>

int main(int argc, char *argv[])
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<SerialJoyBridge>("serial_joy_bridge");
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
