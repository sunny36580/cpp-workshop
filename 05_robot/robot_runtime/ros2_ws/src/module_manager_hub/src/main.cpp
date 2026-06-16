#include "module_manager_hub/module_manager.h"
#include <rclcpp/rclcpp.hpp>

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<ModuleManager>("module_manager_hub");
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}