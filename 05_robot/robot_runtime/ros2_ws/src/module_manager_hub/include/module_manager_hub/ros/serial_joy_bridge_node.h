#ifndef SERIAL_JOY_BRIDGE_NODE_H
#define SERIAL_JOY_BRIDGE_NODE_H

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>

#include "module_manager_hub/common/serial_reader.h"
#include "module_manager_hub/core/joy_bridge_core.h"

/// 摇杆串口桥接节点 —— ROS Node 层
/// - 持有 SerialReader（串口读写）
/// - 持有 JoyBridgeCore（协议解析）
/// - 发布 /joy 话题
class SerialJoyBridgeNode : public rclcpp::Node
{
public:
  explicit SerialJoyBridgeNode(const std::string &name,
                               const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~SerialJoyBridgeNode();

private:
  void onSerialData(const uint8_t *data, size_t len);

  SerialReader serial_reader_;
  JoyBridgeCore joy_core_;

  std::string serial_port_;
  int serial_baud_;

  rclcpp::Publisher<sensor_msgs::msg::Joy>::SharedPtr joy_pub_;
};

#endif // SERIAL_JOY_BRIDGE_NODE_H
