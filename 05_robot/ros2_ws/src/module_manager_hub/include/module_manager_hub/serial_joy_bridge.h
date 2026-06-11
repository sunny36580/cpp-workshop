#ifndef SERIAL_JOY_BRIDGE_H
#define SERIAL_JOY_BRIDGE_H

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <boost/asio.hpp>
#include <vector>
#include <cstdint>
#include <thread>

class SerialJoyBridge : public rclcpp::Node
{
public:
  explicit SerialJoyBridge(const std::string &name);
  ~SerialJoyBridge();

private:
  void doSerialRead();
  bool tryParseFrame();
  void publishJoy(const std::vector<uint8_t> &frame, size_t num_axes, size_t num_btns);

  // 串口 (Boost.Asio)
  boost::asio::io_context io_context_;
  boost::asio::serial_port serial_;
  std::thread io_thread_;
  std::array<uint8_t, 256> read_buf_;
  std::vector<uint8_t> rx_buf_;

  std::string serial_port_;
  int serial_baud_;

  // 发布器
  rclcpp::Publisher<sensor_msgs::msg::Joy>::SharedPtr joy_pub_;

  // 统计
  size_t frame_count_ = 0;
};

#endif // SERIAL_JOY_BRIDGE_H
