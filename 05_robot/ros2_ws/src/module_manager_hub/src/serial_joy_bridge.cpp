#include "module_manager_hub/serial_joy_bridge.h"
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <chrono>
#include <algorithm>

using namespace std::chrono_literals;

static constexpr uint8_t FRAME_SOF = 0xAA;

SerialJoyBridge::SerialJoyBridge(const std::string &name)
  : Node(name), serial_(io_context_)
{
  this->declare_parameter<std::string>("serial_port", "/dev/ttyUSB0");
  this->declare_parameter<int>("serial_baud", 115200);

  serial_port_ = this->get_parameter("serial_port").as_string();
  serial_baud_ = this->get_parameter("serial_baud").as_int();

  joy_pub_ = this->create_publisher<sensor_msgs::msg::Joy>("/joy", 10);

  // 打开串口
  try {
    serial_.open(serial_port_);
    serial_.set_option(boost::asio::serial_port_base::baud_rate(serial_baud_));
    serial_.set_option(boost::asio::serial_port_base::character_size(8));
    serial_.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    serial_.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    serial_.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
    RCLCPP_INFO(this->get_logger(), "串口已打开: %s @ %d baud", serial_port_.c_str(), serial_baud_);
  } catch (const std::exception &e) {
    RCLCPP_ERROR(this->get_logger(), "串口打开失败: %s", e.what());
    return;
  }

  // 启动异步读取
  doSerialRead();
  io_thread_ = std::thread([this]() { io_context_.run(); });
}

SerialJoyBridge::~SerialJoyBridge()
{
  if (serial_.is_open()) {
    boost::system::error_code ec;
    serial_.close(ec);
  }
  io_context_.stop();
  if (io_thread_.joinable()) io_thread_.join();
}

// =====================================================================
// Python 端数据格式（与 _read_joystick_raw 一致）：
//   [0xAA:1B][num_axes:1B][axis0:int16LE][axis1:int16LE]...
//   [num_btns:1B][btn0:uint8][btn1:uint8]...
// =====================================================================

void SerialJoyBridge::doSerialRead()
{
  serial_.async_read_some(boost::asio::buffer(read_buf_),
    [this](boost::system::error_code ec, std::size_t bytes) {
      if (ec) {
        if (rclcpp::ok()) {
          RCLCPP_WARN(this->get_logger(), "串口读错误: %s", ec.message().c_str());
          doSerialRead();
        }
        return;
      }

      rx_buf_.insert(rx_buf_.end(), read_buf_.begin(), read_buf_.begin() + bytes);

      // 尝试解析所有完整帧
      while (tryParseFrame());

      doSerialRead();
    });
}

bool SerialJoyBridge::tryParseFrame()
{
  // 找帧头 0xAA
  auto sof_it = std::find(rx_buf_.begin(), rx_buf_.end(), FRAME_SOF);
  if (sof_it == rx_buf_.end()) {
    rx_buf_.clear();
    return false;
  }

  // 丢掉帧头之前的垃圾数据
  if (sof_it != rx_buf_.begin()) {
    rx_buf_.erase(rx_buf_.begin(), sof_it);
  }

  // 至少需要 SOF(1) + num_axes(1) = 2 字节
  if (rx_buf_.size() < 2) return false;

  uint8_t num_axes = rx_buf_[1];
  // 最小帧: SOF(1) + num_axes(1) + axes(N*2) + num_btns(1)
  size_t min_len = 2 + num_axes * 2 + 1;
  if (rx_buf_.size() < min_len) return false;

  // 读按钮数量
  size_t btn_off = 2 + num_axes * 2;
  uint8_t num_btns = rx_buf_[btn_off];

  // 完整帧长
  size_t frame_len = 2 + num_axes * 2 + 1 + num_btns;
  if (rx_buf_.size() < frame_len) return false;

  // 解析并发布
  std::vector<uint8_t> frame(rx_buf_.begin(), rx_buf_.begin() + frame_len);
  publishJoy(frame, num_axes, num_btns);

  // 移除已处理的帧
  rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + frame_len);

  frame_count_++;
  if (frame_count_ % 300 == 0) {
    RCLCPP_INFO(this->get_logger(), "[JoyBridge] 已接收 %zu 帧, 轴=%d, 按钮=%d, 缓存=%zu",
                frame_count_, num_axes, num_btns, rx_buf_.size());
  }

  return true;  // 可能还有更多帧
}

void SerialJoyBridge::publishJoy(const std::vector<uint8_t> &frame, size_t num_axes, size_t num_btns)
{
  sensor_msgs::msg::Joy joy_msg;
  joy_msg.header.stamp = this->now();

  // 解析轴: int16 LE → float (-32767~32767 → -1.0~1.0)
  joy_msg.axes.reserve(num_axes);
  for (size_t i = 0; i < num_axes; i++) {
    size_t off = 2 + i * 2;  // SOF(1) + num_axes(1) + i*2
    int16_t raw = static_cast<int16_t>(frame[off]) |
                  (static_cast<int16_t>(frame[off + 1]) << 8);
    float val = static_cast<float>(raw) / 32767.0f;
    val = std::max(-1.0f, std::min(1.0f, val));
    joy_msg.axes.push_back(val);
  }

  // 解析按钮
  joy_msg.buttons.reserve(num_btns);
  for (size_t i = 0; i < num_btns; i++) {
    size_t off = 2 + num_axes * 2 + 1 + i;  // SOF + num_axes + axes + num_btns + i
    joy_msg.buttons.push_back(frame[off] ? 1 : 0);
  }

  joy_pub_->publish(joy_msg);
}
