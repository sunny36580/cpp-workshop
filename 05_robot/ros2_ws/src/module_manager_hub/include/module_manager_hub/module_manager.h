#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <unordered_map>
#include <queue>
#include "module.h"
#include "module_manager_hub/msg/module_status.hpp"
#include "module_manager_hub/srv/module_control.hpp"
#include "std_msgs/msg/empty.hpp"

class ModuleManager : public rclcpp::Node {
public:
  explicit ModuleManager(const std::string &name);
  ~ModuleManager() override;

  void handleShutdownSignal(int sig);

private:
  // 配置加载
  void loadConfig(const std::string &path);

  // 核心管控
  bool startModule(const std::string &name);
  bool stopModule(const std::string &name);
  bool restartModule(const std::string &name);
  
  // 依赖启动队列处理
  void processStartupQueue();
  bool checkDependenciesReady(const std::string &name);
  
  // 就绪检查回调
  void readyCallback(const std_msgs::msg::Empty::SharedPtr msg, const std::string &mod_name);

  // 心跳回调
  void heartbeatCallback(const std_msgs::msg::Empty::SharedPtr msg, const std::string &mod_name);

  // 巡检定时器（处理超时、重启、状态机）
  void monitorTimerCallback();

  // 状态发布（包含完整状态）
  void publishModuleStatus();

  // 控制服务（支持更多指令）
  void moduleControlCallback(
    const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
    std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res
  );

  // 优雅关闭处理
  void gracefulShutdown();

  // 自动重启处理
  void handleModuleFailure(const std::string &name, const std::string &reason);

  // 数据结构
  std::map<std::string, Module> modules_;
    // 心跳订阅池
  std::unordered_map<std::string, rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr> heart_subs_;
  std::unordered_map<std::string, rclcpp::Subscription<std_msgs::msg::Empty>::SharedPtr> ready_subs_;
  
  // 启动队列
  std::queue<std::string> startup_queue_;
  bool processing_startup_ = false;

  // ROS接口
  rclcpp::Publisher<module_manager_hub::msg::ModuleStatus>::SharedPtr status_pub_;
  rclcpp::Service<module_manager_hub::srv::ModuleControl>::SharedPtr ctrl_srv_;
  rclcpp::TimerBase::SharedPtr monitor_timer_;
};