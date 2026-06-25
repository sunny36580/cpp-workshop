#pragma once

#include <rclcpp/rclcpp.hpp>
#include <map>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "module_manager_hub/common/serial_reader.h"
#include "module_manager_hub/core/module_manager_core.h"
#include "module_manager_hub/msg/robotarmcontrol.hpp"
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

/// 模块管理器节点 —— ROS Node 层
/// - 持有 SerialReader（串口读写）
/// - 持有 ModuleManagerCore（协议解析 + 速度插值）
/// - 发布 cmd_vel / control_mode / hwswitch / action_cmd 等话题
class ModuleManagerNode : public rclcpp::Node {
public:
  explicit ModuleManagerNode(const std::string &name,
                             const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~ModuleManagerNode() override;

private:
  // 从 YAML 加载配置
  void loadSerialConfig(const YAML::Node &root);
  void loadTrackedModules(const YAML::Node &mods);
  void loadCmdRoute(const YAML::Node &route_node);
  void dispatchCommand(const std::string &cmd, const std::vector<double> &params);

  // SerialReader 裸数据回调 → 交给 ModuleManagerCore 解析
  void onSerialData(const uint8_t *data, size_t len);
  void onParsedPacket(uint8_t cmd_type, const std::vector<uint8_t>& payload);
  void sendSerialStatus();

  SerialReader serial_reader_;
  ModuleManagerCore core_;

  // 订阅/发布器
  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> watch_subs_;
  rclcpp::TimerBase::SharedPtr status_timer_;  // 500ms 定期推送串口状态

  std::map<std::string, CmdRoute> cmd_routes_;
  std::map<std::string, rclcpp::Publisher<module_manager_hub::msg::Robotarmcontrol>::SharedPtr> arm_control_pubs_;

  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr control_mode_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr hw_switch_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_cmd_pub_;

  std::string current_mode_;
  bool hw_switch_state_;

  static constexpr double TASK_CMD_DEBOUNCE_SEC = 2.0;
  double last_task_cmd_time_ = 0.0;
};
