#pragma once

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/bool.hpp>

#include "module_manager_hub/core/camera_streamer_core.h"

/// 相机推流节点 —— ROS Node 层
/// - 订阅 /camera1/image_raw
/// - 持有 CameraStreamerCore（编码 + TCP 推流）
/// - 发布 /camera/status 心跳
class CameraStreamerNode : public rclcpp::Node
{
public:
  explicit CameraStreamerNode(const std::string& node_name,
                              const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~CameraStreamerNode() override;

private:
  void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg);
  void publishHeartbeat();

  CameraStreamerCore core_;
  std::thread stream_thread_;
  std::atomic<bool> running_{true};

  std::string image_topic_;

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr heartbeat_pub_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};
