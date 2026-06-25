#include "module_manager_hub/ros/camera_streamer_node.h"
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <sensor_msgs/msg/image.hpp>

using namespace std::chrono_literals;

CameraStreamerNode::CameraStreamerNode(const std::string& node_name,
                                       const rclcpp::NodeOptions &opts)
    : Node(node_name, opts)
{
  this->declare_parameter<std::string>("image_topic", "/camera1/image_raw");
  this->declare_parameter<int>("port", 8888);
  this->declare_parameter<int>("bitrate", 3000);

  image_topic_ = this->get_parameter("image_topic").as_string();
  int port      = this->get_parameter("port").as_int();
  int bitrate   = this->get_parameter("bitrate").as_int();

  // 设置 Core 层日志回调
  core_.setLogCallback(
      [this](int level, const std::string& msg) {
        switch (level) {
          case 0: RCLCPP_INFO(this->get_logger(), "%s", msg.c_str()); break;
          case 1: RCLCPP_WARN(this->get_logger(), "%s", msg.c_str()); break;
          case 2: RCLCPP_ERROR(this->get_logger(), "%s", msg.c_str()); break;
        }
      });

  image_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
      image_topic_, rclcpp::SensorDataQoS(),
      std::bind(&CameraStreamerNode::imageCallback, this, std::placeholders::_1));

  // 初始化编码器和 TCP server
  core_.initEncoder(640, 480, bitrate);
  core_.initTcpServer(port);

  // 启动推流线程
  auto ok_check = [this]() { return rclcpp::ok(); };
  stream_thread_ = std::thread(&CameraStreamerCore::streamLoop, &core_,
                                std::ref(running_), ok_check);

  RCLCPP_INFO(this->get_logger(), "✅ 推流节点启动  topic=%s  port=%d  H.264",
              image_topic_.c_str(), port);
}

CameraStreamerNode::~CameraStreamerNode()
{
  running_ = false;
  if (stream_thread_.joinable()) stream_thread_.join();
}

void CameraStreamerNode::imageCallback(const sensor_msgs::msg::Image::SharedPtr msg)
{
  try {
    auto cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
    core_.setFrame(cv_ptr->image);
  } catch (cv_bridge::Exception &e) {
    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                         "cv_bridge failed: %s", e.what());
  }
}
