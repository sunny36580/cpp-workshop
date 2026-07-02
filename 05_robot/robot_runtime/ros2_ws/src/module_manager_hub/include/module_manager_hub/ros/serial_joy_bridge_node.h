#ifndef SERIAL_JOY_BRIDGE_NODE_H
#define SERIAL_JOY_BRIDGE_NODE_H

#include <atomic>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>

#include "module_manager_hub/common/serial_reader.h"
#include "module_manager_hub/core/joy_bridge_core.h"

/// 摇杆串口桥接节点
class SerialJoyBridgeNode : public rclcpp::Node
{
public:
  explicit SerialJoyBridgeNode(const std::string &name,
                               const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~SerialJoyBridgeNode();

  /// 设置激活状态（true=发布输出, false=暂停输出，保留资源）
  void setActive(bool on) { active_.store(on); }

private:
  void onSerialData(const uint8_t *data, size_t len);

  SerialReader serial_reader_;
  JoyBridgeCore joy_core_;

  std::string serial_port_;
  int serial_baud_;
  std::atomic<bool> active_{true};

  rclcpp::Publisher<sensor_msgs::msg::Joy>::SharedPtr joy_pub_;
};

#endif // SERIAL_JOY_BRIDGE_NODE_H
