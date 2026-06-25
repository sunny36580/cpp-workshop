#pragma once

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <map>
#include <memory>
#include <string>
#include <yaml-cpp/yaml.h>

#include "module_manager_hub/core/heartbeat_collector_core.h"

/// 心跳汇聚节点 —— ROS Node 层
/// - 为每个 target 建立话题订阅
/// - 持有 HeartbeatCollectorCore（文件写入 + UDP 上报 + 超时检测）
/// - 发布自身心跳到 /robot/collector/heartbeat
class HeartbeatCollectorNode : public rclcpp::Node {
public:
  explicit HeartbeatCollectorNode(const std::string &name,
                                  const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~HeartbeatCollectorNode() override;

private:
  void loadConfig(const YAML::Node &root);
  void onHeartbeat(const std::string &name);
  void publishSelfHeartbeat();
  void checkTimer();
  void sendUdpReport();

  HeartbeatCollectorCore core_;

  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> subs_;
  rclcpp::TimerBase::SharedPtr self_hb_timer_;
  rclcpp::TimerBase::SharedPtr check_timer_;
  rclcpp::TimerBase::SharedPtr udp_timer_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr self_hb_pub_;
};
