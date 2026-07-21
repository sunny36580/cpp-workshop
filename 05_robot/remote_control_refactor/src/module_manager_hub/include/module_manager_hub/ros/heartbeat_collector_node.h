#pragma once

#include <rclcpp/rclcpp.hpp>
#include <map>
#include <memory>
#include <string>
#include <yaml-cpp/yaml.h>

#include "module_manager_hub/core/heartbeat_collector_core.h"

/// 心跳汇聚节点 —— ROS Node 层
/// - 为每个外部 target 建立话题订阅
/// - 同进程内的服务与 CollectorNode 共享生死，无需心跳追踪
/// - 持有 HeartbeatCollectorCore（文件写入 + UDP 上报 + 超时检测）
class HeartbeatCollectorNode : public rclcpp::Node {
public:
  explicit HeartbeatCollectorNode(const std::string &name,
                                  const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~HeartbeatCollectorNode() override;

private:
  void loadConfig(const YAML::Node &root);
  void onHeartbeat(const std::string &name) { core_.onHeartbeat(name, this->now().seconds()); }
  void checkTimer();
  void sendUdpReport();

  HeartbeatCollectorCore core_;

  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> subs_;
  rclcpp::TimerBase::SharedPtr check_timer_;
  rclcpp::TimerBase::SharedPtr udp_timer_;
};
