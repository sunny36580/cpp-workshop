#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include "module.h"
#include "module_manager_hub/msg/module_status.hpp"
#include "module_manager_hub/srv/module_control.hpp"

class ModuleManager : public rclcpp::Node {
public:
  explicit ModuleManager(const std::string &name);

private:
  void loadConfig(const std::string &path);
  bool startModule(const std::string &name);
  bool stopModule(const std::string &name);
  bool restartModule(const std::string &name);

  // 通用话题回调：任何话题有数据就更新时间
  template <typename MsgT>
  void topicCallback(const std::shared_ptr<MsgT>, const std::string &mod_name);

  void monitorTimerCallback();
  void publishModuleStatus();

  void moduleControlCallback(
    const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
    std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res
  );

  std::map<std::string, Module> modules_;
  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> topic_subs_;

  rclcpp::Publisher<module_manager_hub::msg::ModuleStatus>::SharedPtr status_pub_;
  rclcpp::Service<module_manager_hub::srv::ModuleControl>::SharedPtr ctrl_srv_;
  rclcpp::TimerBase::SharedPtr monitor_timer_;
};