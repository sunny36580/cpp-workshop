#include "module_manager_hub/serial_joy_bridge.h"
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/joy.hpp>
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <algorithm>

using namespace std::chrono_literals;

// 帧头 2 字节 (0xAA 0x55)，避免轴数据中的 0xAA 误同步
static constexpr size_t   SOF_BYTES = 2;
static constexpr size_t   CRC_BYTES = 2;
static constexpr size_t   MAX_AXES  = 16;  // 轴数合理性上限
static constexpr size_t   MAX_BTNS  = 64;  // 按钮数合理性上限

// CRC16-Modbus，与 Python 端 calc_crc16 一致
static uint16_t calcCRC16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

SerialJoyBridge::SerialJoyBridge(const std::string &name)
  : Node(name), serial_(io_context_)
{
  // 尝试从 YAML 配置文件加载串口参数
  this->declare_parameter<std::string>("config_path", "");
  std::string config_path = this->get_parameter("config_path").as_string();

  bool config_loaded = false;
  if (!config_path.empty()) {
    try {
      YAML::Node root = YAML::LoadFile(config_path);
      if (root["serial"]) {
        auto s = root["serial"];
        if (s["port"])      serial_port_ = s["port"].as<std::string>();
        if (s["baud_rate"]) serial_baud_ = s["baud_rate"].as<int>();
        config_loaded = true;
        RCLCPP_INFO(this->get_logger(), "从配置文件加载串口: %s @ %d baud",
                    serial_port_.c_str(), serial_baud_);
      }
    } catch (const std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "加载配置文件失败: %s", e.what());
    }
  }

  // 如果 YAML 未加载成功，回退到 ROS2 参数
  if (!config_loaded) {
    this->declare_parameter<std::string>("serial_port", "/dev/ttyUSB0");
    this->declare_parameter<int>("serial_baud", 115200);
    serial_port_ = this->get_parameter("serial_port").as_string();
    serial_baud_ = this->get_parameter("serial_baud").as_int();
  }

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
//   [0xAA 0x55:2B][num_axes:1B][axis0:int16LE][axis1:int16LE]...
//   [num_btns:1B][btn0:uint8][btn1:uint8]...[CRC16:2B LE]
//   CRC 校验范围：SOF 之后的所有数据（从 num_axes 到最后一个按钮）
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
  // 找帧头 0xAA 0x55 (2字节)
  if (rx_buf_.size() < SOF_BYTES) return false;

  const uint8_t sof_bytes[SOF_BYTES] = {0xAA, 0x55};
  auto it = std::search(rx_buf_.begin(), rx_buf_.end(),
                        sof_bytes, sof_bytes + SOF_BYTES);
  if (it == rx_buf_.end()) {
    // 未找到帧头，清空缓存
    rx_buf_.clear();
    return false;
  }

  // 丢掉帧头之前的垃圾数据
  if (it != rx_buf_.begin()) {
    rx_buf_.erase(rx_buf_.begin(), it);
  }

  // 至少需要 SOF(2) + num_axes(1) = 3 字节
  if (rx_buf_.size() < SOF_BYTES + 1) return false;

  uint8_t num_axes = rx_buf_[SOF_BYTES];

  // ---- 合理性校验 ----
  if (num_axes == 0 || num_axes > MAX_AXES) {
    // 无效轴数，跳过头两个字节继续搜索
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 1);
    return true;  // 继续尝试
  }

  // 最小帧: SOF(2) + num_axes(1) + axes(N*2) + num_btns(1)
  size_t min_len = SOF_BYTES + 1 + num_axes * 2 + 1;
  if (rx_buf_.size() < min_len) return false;

  // 读按钮数量
  size_t btn_off = SOF_BYTES + 1 + num_axes * 2;
  uint8_t num_btns = rx_buf_[btn_off];

  // ---- 按钮数合理性校验 ----
  if (num_btns > MAX_BTNS) {
    // 无效按钮数，说明帧同步丢失，跳过头两个字节
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 1);
    return true;
  }

  // 完整帧长（含 CRC16 2 字节）
  size_t frame_len = SOF_BYTES + 1 + num_axes * 2 + 1 + num_btns + CRC_BYTES;
  if (rx_buf_.size() < frame_len) return false;

  // ---- CRC16 校验 ----
  // CRC 校验范围：从 num_axes 到最后一个按钮（不含 CRC 本身）
  size_t crc_data_len = 1 + num_axes * 2 + 1 + num_btns;  // num_axes + axes + num_btns + buttons
  uint16_t recv_crc = static_cast<uint16_t>(rx_buf_[frame_len - 2]) |
                      (static_cast<uint16_t>(rx_buf_[frame_len - 1]) << 8);
  uint16_t calc_crc = calcCRC16(&rx_buf_[SOF_BYTES], crc_data_len);
  if (recv_crc != calc_crc) {
    // CRC 错误，可能是帧同步丢失，跳过一个字节继续搜索
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 1);
    return true;
  }

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
    size_t off = SOF_BYTES + 1 + i * 2;  // SOF(2) + num_axes(1) + i*2
    int16_t raw = static_cast<int16_t>(frame[off]) |
                  (static_cast<int16_t>(frame[off + 1]) << 8);
    float val = static_cast<float>(raw) / 32767.0f;
    val = std::max(-1.0f, std::min(1.0f, val));
    joy_msg.axes.push_back(val);
  }

  // 解析按钮
  joy_msg.buttons.reserve(num_btns);
  for (size_t i = 0; i < num_btns; i++) {
    size_t off = SOF_BYTES + 1 + num_axes * 2 + 1 + i;
    joy_msg.buttons.push_back(frame[off] ? 1 : 0);
  }

  joy_pub_->publish(joy_msg);
}
