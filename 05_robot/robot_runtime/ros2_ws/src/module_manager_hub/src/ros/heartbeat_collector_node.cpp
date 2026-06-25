#include "module_manager_hub/ros/heartbeat_collector_node.h"
#include <chrono>
#include <filesystem>
#include <yaml-cpp/yaml.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// 构造 & 析构
HeartbeatCollectorNode::HeartbeatCollectorNode(const std::string &name,
                                               const rclcpp::NodeOptions &opts)
  : Node(name, opts)
{
  // 设置 Core 层日志回调
  core_.setLogCallback(
      [this](int level, const std::string& msg) {
        switch (level) {
          case 0: RCLCPP_INFO(this->get_logger(), "%s", msg.c_str()); break;
          case 1: RCLCPP_WARN(this->get_logger(), "%s", msg.c_str()); break;
          case 2: RCLCPP_ERROR(this->get_logger(), "%s", msg.c_str()); break;
        }
      });

  // 加载配置
  this->declare_parameter<std::string>("config_path", "");
  std::string cfg_path = this->get_parameter("config_path").as_string();
  if (!cfg_path.empty() && fs::exists(cfg_path)) {
    try {
      YAML::Node root = YAML::LoadFile(cfg_path);
      loadConfig(root);
    } catch (std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "加载配置文件失败: %s", e.what());
    }
  }

  // 没加载到配置时用测试默认值
  if (core_.targets().empty()) {
    HeartbeatCollectorCore::HeartbeatTarget t;
    t.name = "test_service";
    t.topic = "/robot/test/heartbeat";
    t.msg_type = "std_msgs/msg/String";
    core_.targets().push_back(t);
  }

  // 自身心跳发布
  self_hb_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/robot/collector/heartbeat", 10);

  // 为每个 target 建立订阅
  for (auto &t : core_.targets()) {
    if (t.topic.empty()) continue;
    try {
      auto cb = [this, n = t.name](const std::shared_ptr<rclcpp::SerializedMessage>) {
        onHeartbeat(n);
      };
      std::string type = t.msg_type.empty() ? "std_msgs/msg/String" : t.msg_type;
      subs_[t.name] = this->create_generic_subscription(t.topic, type, 10, cb);
      RCLCPP_INFO(this->get_logger(), "心跳监听: %s ← %s [%s]",
                  t.name.c_str(), t.topic.c_str(), type.c_str());
    } catch (std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "订阅失败 %s: %s", t.topic.c_str(), e.what());
    }
  }

  // UDP 初始化
  core_.initUdpSocket();

  // 定时 1s 发布自身心跳
  self_hb_timer_ = this->create_wall_timer(1s,
      std::bind(&HeartbeatCollectorNode::publishSelfHeartbeat, this));

  // 定时 2s 检查超时
  check_timer_ = this->create_wall_timer(2s,
      std::bind(&HeartbeatCollectorNode::checkTimer, this));

  // 定时 2s UDP 上报
  udp_timer_ = this->create_wall_timer(2s,
      std::bind(&HeartbeatCollectorNode::sendUdpReport, this));

  RCLCPP_INFO(this->get_logger(), "心跳汇聚节点启动 ✅");
}

HeartbeatCollectorNode::~HeartbeatCollectorNode()
{
}

// 配置加载
void HeartbeatCollectorNode::loadConfig(const YAML::Node &root)
{
  auto hb = root["heartbeat"];
  if (!hb) return;

  std::string heartbeat_dir;
  double timeout_sec = 8.0;
  std::string udp_host;
  int udp_port = 0;
  std::vector<HeartbeatCollectorCore::HeartbeatTarget> targets;

  if (hb["heartbeat_dir"]) heartbeat_dir = hb["heartbeat_dir"].as<std::string>();
  if (hb["timeout_sec"])   timeout_sec   = hb["timeout_sec"].as<double>();
  if (hb["udp_host"])      udp_host      = hb["udp_host"].as<std::string>();
  if (hb["udp_port"])      udp_port      = hb["udp_port"].as<int>();

  if (hb["targets"]) {
    for (const auto &entry : hb["targets"]) {
      HeartbeatCollectorCore::HeartbeatTarget t;
      t.name      = entry["name"].as<std::string>();
      t.topic     = entry["topic"].as<std::string>();
      t.msg_type  = entry["msg_type"] ? entry["msg_type"].as<std::string>() : "std_msgs/msg/String";
      t.svc_id    = entry["svc_id"] ? entry["svc_id"].as<uint16_t>() : 0;
      targets.push_back(t);
    }
  }

  core_.loadConfig(heartbeat_dir, timeout_sec, udp_host, udp_port, targets);
}

// 节点层心跳接收
void HeartbeatCollectorNode::onHeartbeat(const std::string &name)
{
  core_.onHeartbeat(name, this->now().seconds());
}

// 自身心跳发布
void HeartbeatCollectorNode::publishSelfHeartbeat()
{
  auto msg = std_msgs::msg::String();
  msg.data = "alive";
  self_hb_pub_->publish(msg);

  // 更新自身在线状态
  onHeartbeat("heartbeat_collector");
}

// 超时检测
void HeartbeatCollectorNode::checkTimer()
{
  core_.checkTimeout(this->now().seconds());
}

// UDP 上报
void HeartbeatCollectorNode::sendUdpReport()
{
  core_.sendUdpReport();
}
