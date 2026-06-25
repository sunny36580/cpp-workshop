#include "module_manager_hub/ros/module_manager_node.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdio>
#include <algorithm>

using namespace std::chrono_literals;

// =====================================================================
// 构造函数 & 析构
// =====================================================================
ModuleManagerNode::ModuleManagerNode(const std::string &name,
                                     const rclcpp::NodeOptions &opts)
  : Node(name, opts), current_mode_("STAND"), hw_switch_state_(false)
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
  std::string config_path = "config/modules.yaml";
  this->declare_parameter<std::string>("config_path", config_path);
  this->get_parameter("config_path", config_path);
  YAML::Node root_cfg = YAML::LoadFile(config_path);

  loadSerialConfig(root_cfg);
  loadTrackedModules(root_cfg["modules"]);
  loadCmdRoute(root_cfg["cmd_route"]);

  // 遥控话题
  cmd_vel_pub_     = this->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
  control_mode_pub_ = this->create_publisher<std_msgs::msg::String>("/control_mode", 10);
  hw_switch_pub_   = this->create_publisher<std_msgs::msg::Bool>("/hwswitch", 10);
  action_cmd_pub_  = this->create_publisher<std_msgs::msg::String>("/action_cmd", 10);

  // 设置 SerialReader 裸数据回调 → 交给 ModuleManagerCore 解析
  serial_reader_.setDataCallback(
      std::bind(&ModuleManagerNode::onSerialData, this,
                std::placeholders::_1, std::placeholders::_2));

  // 500ms 定时推送串口状态
  status_timer_ = this->create_wall_timer(500ms,
      std::bind(&ModuleManagerNode::sendSerialStatus, this));

  // 启动速度插值线程
  core_.startSpeedLoop();

  // 打开机器人遥控串口
  if (root_cfg["serial"]) {
    auto s = root_cfg["serial"];
    std::string port = s["port"] ? s["port"].as<std::string>() : "/dev/ttyUSB0";
    int baud = s["baud_rate"] ? s["baud_rate"].as<int>() : 115200;
    serial_reader_.open(port, baud);
  }

  RCLCPP_INFO(this->get_logger(), "串口 ↔ ROS 桥接启动完成 ✅");
}

ModuleManagerNode::~ModuleManagerNode()
{
  core_.stopSpeedLoop();
  serial_reader_.close();
}

// =====================================================================
// 配置加载
// =====================================================================
void ModuleManagerNode::loadSerialConfig(const YAML::Node &root)
{
  if (root["serial"]) {
    auto s = root["serial"];
    std::string port = s["port"] ? s["port"].as<std::string>() : "/dev/ttyUSB0";
    int baud = s["baud_rate"] ? s["baud_rate"].as<int>() : 115200;
    RCLCPP_INFO(this->get_logger(), "机器人遥控串口: %s @ %d baud", port.c_str(), baud);
  }
}

void ModuleManagerNode::loadTrackedModules(const YAML::Node &mods)
{
  if (!mods) return;

  for (const auto &entry : mods) {
    try {
      auto &cfg = entry.second;
      TrackedModule tm;
      tm.name        = entry.first.as<std::string>();
      tm.watch_topic = cfg["watch_topic"] ? cfg["watch_topic"].as<std::string>() : "";
      tm.watch_type  = cfg["watch_type"] ? cfg["watch_type"].as<std::string>() : "";
      tm.last_msg_time = this->now().seconds();
      core_.trackedModules()[tm.name] = tm;

      // 为有 watch_topic 的模块建立话题订阅（用于心跳状态追踪）
      if (!tm.watch_topic.empty()) {
        try {
          auto cb = [this, n = tm.name](const std::shared_ptr<rclcpp::SerializedMessage>) {
            core_.updateModuleHeartbeat(n, this->now().seconds());
          };
          std::string type_str = tm.watch_type.empty() ? "std_msgs/msg/String" : tm.watch_type;
          watch_subs_[tm.name] = this->create_generic_subscription(tm.watch_topic, type_str, 10, cb);
          RCLCPP_INFO(this->get_logger(), "追踪模块 %s → %s [%s]", tm.name.c_str(), tm.watch_topic.c_str(), type_str.c_str());
        } catch (std::exception &e) {
          RCLCPP_WARN(this->get_logger(), "订阅失败 %s: %s", tm.watch_topic.c_str(), e.what());
        }
      }
    } catch (std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "跳过无效追踪模块: %s", e.what());
    }
  }
}

// =====================================================================
// 指令路由
// =====================================================================
void ModuleManagerNode::loadCmdRoute(const YAML::Node &route_node)
{
  for (const auto &entry : route_node) {
    CmdRoute r;
    r.topic    = entry.second["topic"].as<std::string>();
    r.msg_type = entry.second["msg_type"].as<std::string>();
    cmd_routes_[entry.first.as<std::string>()] = r;
    if (r.msg_type == "Robotarmcontrol")
      arm_control_pubs_[r.topic] = this->create_publisher<module_manager_hub::msg::Robotarmcontrol>(r.topic, 10);
    RCLCPP_INFO(this->get_logger(), "指令路由: %s → %s [%s]", entry.first.as<std::string>().c_str(), r.topic.c_str(), r.msg_type.c_str());
  }
}

// =====================================================================
// SerialReader 裸数据回调 → Core 解析 → 节点层处理
// =====================================================================
void ModuleManagerNode::onSerialData(const uint8_t *data, size_t len)
{
  auto packets = core_.feedSerialData(data, len);
  for (const auto& [cmd_type, payload] : packets) {
    onParsedPacket(cmd_type, payload);
  }
}

void ModuleManagerNode::onParsedPacket(uint8_t cmd_type, const std::vector<uint8_t>& payload)
{
  auto publish_mode = [this]() {
    std_msgs::msg::String m; m.data = current_mode_; control_mode_pub_->publish(m);
  };

  switch (cmd_type)
  {
    // ---- CMD_MOVE: 运动指令 ----
    case CMD_MOVE: {
      if (payload.size() < 8) { RCLCPP_WARN(this->get_logger(), "运动指令长度不足"); break; }
      float linear = 0, angular = 0;
      memcpy(&linear,  payload.data(),      sizeof(float));
      memcpy(&angular, payload.data() + 4,  sizeof(float));

      core_.setTargetSpeed(linear, angular);

      if ((std::abs(linear) > 0.001f || std::abs(angular) > 0.001f) && current_mode_ == "STAND") {
        current_mode_ = "WALK_FULL";
        publish_mode();
      }

      float clamped_linear = std::clamp(linear, -0.6f, 0.6f);
      float clamped_angular = std::clamp(angular, -1.0f, 1.0f);
      RCLCPP_INFO(this->get_logger(), "运动指令: linear=%.3f, angular=%.3f",
                  clamped_linear, clamped_angular);
      break;
    }

    // ---- CMD_TASK: 任务指令 ----
    case CMD_TASK: {
      double now = this->now().seconds();
      if (now - last_task_cmd_time_ < TASK_CMD_DEBOUNCE_SEC) {
        RCLCPP_WARN(this->get_logger(), "防误触: 忽略连续任务指令 (距上次 %.2fs < %.1fs)",
                    now - last_task_cmd_time_, TASK_CMD_DEBOUNCE_SEC);
        break;
      }
      last_task_cmd_time_ = now;

      if (payload.size() < 1) { RCLCPP_WARN(this->get_logger(), "任务指令长度不足"); break; }
      uint8_t task_id = payload[0];

      static const char* actions[] = {"WAVE","HANDSHAKE","PERFORM","TURN_180","FINGER_SHOW","WAVE","SMILE","STAND"};
      if (task_id >= 1 && task_id <= 8) {
        std_msgs::msg::String a;
        a.data = actions[task_id - 1];
        action_cmd_pub_->publish(a);
        RCLCPP_INFO(this->get_logger(), "转发 action_cmd: %s", a.data.c_str());
      }
      break;
    }

    // ---- CMD_HEARTBEAT: 急停 ----
    case CMD_HEARTBEAT: {
      RCLCPP_WARN(this->get_logger(), ">>> 急停 <<<");
      core_.setTargetSpeed(0.0f, 0.0f);
      cmd_vel_pub_->publish(geometry_msgs::msg::Twist());
      current_mode_ = "EMERGENCY"; publish_mode();
      hw_switch_state_ = false;
      std_msgs::msg::Bool hw; hw.data = false; hw_switch_pub_->publish(hw);
      break;
    }

    default:
      RCLCPP_WARN(this->get_logger(), "未知指令类型: %d", cmd_type);
  }
}

void ModuleManagerNode::dispatchCommand(const std::string &cmd, const std::vector<double> &params)
{
  auto it = cmd_routes_.find(cmd);
  if (it == cmd_routes_.end()) { RCLCPP_WARN(this->get_logger(), "未知指令: %s", cmd.c_str()); return; }

  if (it->second.msg_type == "Robotarmcontrol") {
    auto pit = arm_control_pubs_.find(it->second.topic);
    if (pit == arm_control_pubs_.end()) return;
    module_manager_hub::msg::Robotarmcontrol msg;
    for (size_t i = 0; i + 2 < params.size(); i += 3) {
      msg.motor_ids.push_back(static_cast<uint32_t>(params[i]));
      msg.target_positions.push_back(static_cast<float>(params[i+1]));
      msg.target_velocitie.push_back(static_cast<float>(params[i+2]));
    }
    pit->second->publish(msg);
    RCLCPP_INFO(this->get_logger(), "转发机械臂指令 ✅ 电机数=%zu", msg.motor_ids.size());
  }
}

// =====================================================================
// 串口状态反馈（500ms 定时向遥控端回发模块状态位图）
// =====================================================================
void ModuleManagerNode::sendSerialStatus()
{
  static const char* mod_ids[] = {
    "lower_body", "upper_body", "imu_driver",
    "remote_interface", "usb_camera", nullptr
  };
  uint16_t mask = 0;
  for (int i = 0; mod_ids[i] != nullptr; i++) {
    auto it = core_.trackedModules().find(mod_ids[i]);
    if (it != core_.trackedModules().end() && it->second.online)
      mask |= (1 << i);
  }

  uint8_t payload[PROTO_DATA_LEN] = {0};
  payload[0] = static_cast<uint8_t>((mask >> 8) & 0xFF);
  payload[1] = static_cast<uint8_t>(mask & 0xFF);

  std::vector<uint8_t> frame;
  core_.buildSerialFrame(CMD_STATUS, payload, PROTO_DATA_LEN, frame);
  serial_reader_.write(frame.data(), frame.size());
}
