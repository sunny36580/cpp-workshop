#pragma once

#include <atomic>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "module_manager_hub/core/camera_streamer_core.h"

/// 相机推流节点
class CameraStreamerNode : public rclcpp::Node
{
public:
  explicit CameraStreamerNode(const std::string& node_name,
                              const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~CameraStreamerNode() override;

  /// 设置激活状态（true=推流, false=暂停推流，保留资源）
  void setActive(bool on) { active_.store(on); }

private:
  void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg);

  CameraStreamerCore core_;
  std::thread stream_thread_;
  std::atomic<bool> running_{true};

  std::string image_topic_;
  std::atomic<bool> active_{true};

  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
};
