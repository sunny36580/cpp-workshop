#include "module_manager_hub/ros/serial_joy_bridge_node.h"
#include <rclcpp/rclcpp.hpp>
#include <yaml-cpp/yaml.h>

using namespace std::chrono_literals;

SerialJoyBridgeNode::SerialJoyBridgeNode(const std::string &name,
                                         const rclcpp::NodeOptions &opts)
  : Node(name, opts)
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

  // 设置 SerialReader 回调 → 交给 JoyBridgeCore 解析
  serial_reader_.setDataCallback(
      std::bind(&SerialJoyBridgeNode::onSerialData, this,
                std::placeholders::_1, std::placeholders::_2));

  // 打开串口
  if (!serial_reader_.open(serial_port_, serial_baud_)) {
    RCLCPP_ERROR(this->get_logger(), "串口打开失败: %s", serial_port_.c_str());
  }
}

SerialJoyBridgeNode::~SerialJoyBridgeNode()
{
  serial_reader_.close();
}

void SerialJoyBridgeNode::onSerialData(const uint8_t *data, size_t len)
{
  auto frames = joy_core_.feedData(data, len);

  for (const auto& frame : frames) {
    sensor_msgs::msg::Joy joy_msg;
    joy_msg.header.stamp = this->now();
    joy_msg.axes = frame.axes;
    joy_msg.buttons = frame.buttons;
    joy_pub_->publish(joy_msg);
  }

  if (joy_core_.frameCount() % 300 == 0 && !frames.empty()) {
    RCLCPP_INFO(this->get_logger(), "[JoyBridge] 已接收 %zu 帧, 缓存=%zu",
                joy_core_.frameCount(), 0UL);
  }
}
