#include "module_manager_hub/ros/camera_streamer_node.h"
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

  // 启动推流（active_ 控制是否实际推数据）
  int port    = this->get_parameter("port").as_int();
  int bitrate = this->get_parameter("bitrate").as_int();
  core_.initEncoder(640, 480, bitrate);
  core_.initTcpServer(port);
  auto ok_check = [this]() { return rclcpp::ok(); };
  stream_thread_ = std::thread(&CameraStreamerCore::streamLoop, &core_,
                                std::ref(running_), ok_check);
}

CameraStreamerNode::~CameraStreamerNode()
{
  running_ = false;
  if (stream_thread_.joinable()) stream_thread_.join();
}

void CameraStreamerNode::imageCallback(const sensor_msgs::msg::Image::SharedPtr msg)
{
  if (!active_.load()) return;  // 停用时丢掉图像，streamLoop 无新帧可推

  // 手动将 ROS Image 转为 cv::Mat，不依赖 cv_bridge
  if (msg->encoding == "bgr8" || msg->encoding == "rgb8") {
    cv::Mat frame(msg->height, msg->width, CV_8UC3,
                  const_cast<uint8_t*>(msg->data.data()), msg->step);
    if (msg->encoding == "rgb8") {
      cv::cvtColor(frame, frame, cv::COLOR_RGB2BGR);
    }
    core_.setFrame(frame);
  } else {
    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                         "不支持的图像编码: %s", msg->encoding.c_str());
  }
}
