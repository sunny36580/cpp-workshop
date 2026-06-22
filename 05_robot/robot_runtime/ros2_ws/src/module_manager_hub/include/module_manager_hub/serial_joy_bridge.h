#ifndef SERIAL_JOY_BRIDGE_H
#define SERIAL_JOY_BRIDGE_H

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include "module_manager_hub/serial_reader.h"
#include <vector>
#include <cstdint>

class SerialJoyBridge : public rclcpp::Node
{
public:
  explicit SerialJoyBridge(const std::string &name,
                           const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~SerialJoyBridge();

private:
  // SerialReader 裸数据回调 → 拼帧缓存 → 协议解析
  void onSerialData(const uint8_t *data, size_t len);
  bool tryParseFrame();
  void publishJoy(const std::vector<uint8_t> &frame, size_t num_axes, size_t num_btns);

  // 共享串口读取器
  SerialReader serial_reader_;
  std::vector<uint8_t> rx_buf_;  // 帧缓存（跨多次读取拼接）

  std::string serial_port_;
  int serial_baud_;

  // 发布器
  rclcpp::Publisher<sensor_msgs::msg::Joy>::SharedPtr joy_pub_;

  // 统计
  size_t frame_count_ = 0;
};

#endif // SERIAL_JOY_BRIDGE_H
