#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <unordered_map>
#include "module.h"
#include "module_manager_hub/msg/module_status.hpp"
#include "module_manager_hub/srv/module_control.hpp"
#include "std_msgs/msg/empty.hpp"

class ModuleManager : public rclcpp::Node {
public:
  explicit ModuleManager(const std::string &name);

private:
  // 配置加载
  void loadConfig(const std::string &path);

  // 核心管控
  bool startModule(const std::string &name);
  bool stopModule(const std::string &name);
  bool restartModule(const std::string &name);

  // 心跳回调
  void heartbeatCallback(const std_msgs::msg::Empty::SharedPtr msg, const std::string &mod_name);

  // 巡检定时器
  void monitorTimerCallback();

  // 状态发布
  void publishModuleStatus();

  // 控制服务
  void moduleControlCallback(
    const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
    std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res
  );

  std::map<std::string, Module> modules_;
    // 心跳订阅池
  std::unordered_map<std::string, rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr> heart_subs_;

  rclcpp::Publisher<module_manager_hub::msg::ModuleStatus>::SharedPtr status_pub_;
  rclcpp::Service<module_manager_hub::srv::ModuleControl>::SharedPtr ctrl_srv_;
  rclcpp::TimerBase::SharedPtr monitor_timer_;
};