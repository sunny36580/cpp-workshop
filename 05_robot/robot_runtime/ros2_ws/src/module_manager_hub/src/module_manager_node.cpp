#include "module_manager_hub/module_manager.h"
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdio>
#include <algorithm>

using namespace std::chrono_literals;

// =====================================================================
// CRC16-Modbus（32 字节固定帧协议用）
// =====================================================================
static uint16_t calcCRC16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else              crc >>= 1;
    }
  }
  return crc;
}

// =====================================================================
// 构造函数 & 析构
// =====================================================================
ModuleManager::ModuleManager(const std::string &name,
                             const rclcpp::NodeOptions &opts)
  : Node(name, opts), current_mode_("STAND"), hw_switch_state_(false)
{
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

  // 设置 SerialReader 裸数据回调 → 在 node 内完成 32B 帧解析
  serial_reader_.setDataCallback(
      std::bind(&ModuleManager::onSerialData, this,
                std::placeholders::_1, std::placeholders::_2));

  // 500ms 定时推送串口状态
  status_timer_ = this->create_wall_timer(500ms,
      std::bind(&ModuleManager::sendSerialStatus, this));

  // 80Hz 速度插值线程
  speed_running_ = true;
  speed_thread_ = std::thread(&ModuleManager::interpolateSpeedLoop, this);

  // 打开机器人遥控串口
  if (root_cfg["serial"]) {
    auto s = root_cfg["serial"];
    std::string port = s["port"] ? s["port"].as<std::string>() : "/dev/ttyUSB0";
    int baud = s["baud_rate"] ? s["baud_rate"].as<int>() : 115200;
    serial_reader_.open(port, baud);
  }

  RCLCPP_INFO(this->get_logger(), "串口 ↔ ROS 桥接启动完成 ✅");
}

ModuleManager::~ModuleManager()
{
  speed_running_ = false;
  if (speed_thread_.joinable()) speed_thread_.join();
  serial_reader_.close();
}

// =====================================================================
// 配置加载
// =====================================================================
void ModuleManager::loadSerialConfig(const YAML::Node &root)
{
  if (root["serial"]) {
    auto s = root["serial"];
    std::string port = s["port"] ? s["port"].as<std::string>() : "/dev/ttyUSB0";
    int baud = s["baud_rate"] ? s["baud_rate"].as<int>() : 115200;
    RCLCPP_INFO(this->get_logger(), "机器人遥控串口: %s @ %d baud", port.c_str(), baud);
  }
}

void ModuleManager::loadTrackedModules(const YAML::Node &mods)
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
      tracked_modules_[tm.name] = tm;

      // 为有 watch_topic 的模块建立话题订阅（用于心跳状态追踪）
      if (!tm.watch_topic.empty()) {
        try {
          auto cb = [this, n = tm.name](const std::shared_ptr<rclcpp::SerializedMessage>) {
            auto it = tracked_modules_.find(n);
            if (it != tracked_modules_.end()) {
              it->second.last_msg_time = this->now().seconds();
              it->second.online = true;
            }
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
void ModuleManager::loadCmdRoute(const YAML::Node &route_node)
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
// 32 字节固定帧串口协议解析（内联在 ModuleManager）
// =====================================================================
void ModuleManager::onSerialData(const uint8_t *data, size_t len)
{
  rx_frame_.insert(rx_frame_.end(), data, data + len);

  // 尝试从缓存中解析完整帧（32 字节固定帧长）
  while (true) {
    auto &buf = rx_frame_;
    if (buf.size() < PROTO_FRAME_LEN) break;

    auto sof_it = std::search(buf.begin(), buf.end(),
                              sof_marker_, sof_marker_ + 2);
    if (sof_it == buf.end()) { buf.clear(); break; }
    if (sof_it != buf.begin()) buf.erase(buf.begin(), sof_it);
    if (buf.size() < PROTO_FRAME_LEN) break;

    // CRC16 校验（从 CmdType 到保留末尾，共 30 字节）
    uint16_t recv_crc = static_cast<uint16_t>(buf[PROTO_FRAME_LEN - 2]) |
                        (static_cast<uint16_t>(buf[PROTO_FRAME_LEN - 1]) << 8);
    uint16_t calc_crc = calcCRC16(buf.data() + 2, PROTO_FRAME_LEN - 4);

    if (recv_crc != calc_crc) {
      RCLCPP_WARN(this->get_logger(), "32B帧 CRC16 错误, 丢弃");
      buf.erase(buf.begin(), buf.begin() + PROTO_FRAME_LEN);
      continue;
    }

    uint8_t cmd_type = buf[2];
    uint8_t pay_len  = buf[3];
    parseSerialPacket(cmd_type, buf.data() + PROTO_HEADER_LEN,
                      std::min(pay_len, static_cast<uint8_t>(PROTO_DATA_LEN)));

    buf.erase(buf.begin(), buf.begin() + PROTO_FRAME_LEN);
  }
}

void ModuleManager::parseSerialPacket(uint8_t cmd_type, const uint8_t *payload, size_t pay_len)
{
  auto publish_mode = [this]() {
    std_msgs::msg::String m; m.data = current_mode_; control_mode_pub_->publish(m);
  };

  switch (cmd_type)
  {
    // ---- CMD_MOVE: 运动指令 ---- 
    case CMD_MOVE: {
      if (pay_len < 8) { RCLCPP_WARN(this->get_logger(), "运动指令长度不足"); break; }
      float linear = 0, angular = 0;
      memcpy(&linear,  payload,      sizeof(float));
      memcpy(&angular, payload + 4,  sizeof(float));

      target_linear_  = std::clamp(linear,  -static_cast<float>(LINEAR_MAX),   static_cast<float>(LINEAR_MAX));
      target_angular_ = std::clamp(angular, -static_cast<float>(ANGULAR_MAX), static_cast<float>(ANGULAR_MAX));

      if ((std::abs(target_linear_) > 0.001f || std::abs(target_angular_) > 0.001f) && current_mode_ == "STAND") {
        current_mode_ = "WALK_FULL";
        publish_mode();
      }
      RCLCPP_INFO(this->get_logger(), "运动指令: linear=%.3f, angular=%.3f", target_linear_.load(), target_angular_.load());
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

      if (pay_len < 1) { RCLCPP_WARN(this->get_logger(), "任务指令长度不足"); break; }
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
      target_linear_ = 0.0f;  target_angular_ = 0.0f;
      current_linear_ = 0.0f; current_angular_ = 0.0f;
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

void ModuleManager::dispatchCommand(const std::string &cmd, const std::vector<double> &params)
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
// 速度插值独立线程：80Hz 以恒定加减速逼近目标速度
// =====================================================================
void ModuleManager::interpolateSpeedLoop()
{
  rclcpp::Rate rate(SPEED_INTERPOLATE_HZ);
  constexpr double dt = 1.0 / SPEED_INTERPOLATE_HZ;

  while (rclcpp::ok() && speed_running_) {
    float tgt_linear  = target_linear_.load(std::memory_order_relaxed);
    float tgt_angular = target_angular_.load(std::memory_order_relaxed);

    if (std::abs(tgt_linear - current_linear_) < 0.001f) {
      current_linear_ = tgt_linear;
    } else {
      float step = LINEAR_ACCEL * dt;
      current_linear_ = (tgt_linear > current_linear_)
          ? std::min(current_linear_ + step, tgt_linear)
          : std::max(current_linear_ - step, tgt_linear);
    }

    if (std::abs(tgt_angular - current_angular_) < 0.001f) {
      current_angular_ = tgt_angular;
    } else {
      float step = ANGULAR_ACCEL * dt;
      current_angular_ = (tgt_angular > current_angular_)
          ? std::min(current_angular_ + step, tgt_angular)
          : std::max(current_angular_ - step, tgt_angular);
    }

    bool both_zero = (std::abs(current_linear_) < 0.001f && std::abs(current_angular_) < 0.001f &&
                      std::abs(tgt_linear) < 0.001f && std::abs(tgt_angular) < 0.001f);

    if (!both_zero) {
      geometry_msgs::msg::Twist t;
      t.linear.x = current_linear_;
      t.linear.y = 0.0;
      t.angular.z = current_angular_;
      cmd_vel_pub_->publish(t);
    }

    rate.sleep();
  }
}

// =====================================================================
// 组帧（32 字节固定帧）
// =====================================================================
void ModuleManager::buildSerialFrame(uint8_t cmd_type, const uint8_t *payload,
                                      size_t payload_len, std::vector<uint8_t> &out)
{
  out.clear();
  out.reserve(PROTO_FRAME_LEN);
  out.push_back(PROTO_SOF0);
  out.push_back(PROTO_SOF1);
  out.push_back(cmd_type);
  out.push_back(static_cast<uint8_t>(std::min(payload_len, PROTO_DATA_LEN)));

  uint8_t data_field[PROTO_DATA_LEN] = {0};
  size_t copy_len = std::min(payload_len, PROTO_DATA_LEN);
  std::memcpy(data_field, payload, copy_len);
  out.insert(out.end(), data_field, data_field + PROTO_DATA_LEN);

  uint8_t reserved[PROTO_RESERVED] = {0};
  out.insert(out.end(), reserved, reserved + PROTO_RESERVED);

  uint16_t crc = calcCRC16(out.data() + 2, PROTO_FRAME_LEN - 4);
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

// =====================================================================
// 串口状态反馈（500ms 定时向遥控端回发模块状态位图）
// =====================================================================
void ModuleManager::sendSerialStatus()
{
  static const char* mod_ids[] = {
    "lower_body", "upper_body", "imu_driver",
    "remote_interface", "usb_camera", nullptr
  };
  uint16_t mask = 0;
  for (int i = 0; mod_ids[i] != nullptr; i++) {
    auto it = tracked_modules_.find(mod_ids[i]);
    if (it != tracked_modules_.end() && it->second.online)
      mask |= (1 << i);
  }

  uint8_t payload[PROTO_DATA_LEN] = {0};
  payload[0] = static_cast<uint8_t>((mask >> 8) & 0xFF);
  payload[1] = static_cast<uint8_t>(mask & 0xFF);

  std::vector<uint8_t> frame;
  buildSerialFrame(CMD_STATUS, payload, PROTO_DATA_LEN, frame);
  serial_reader_.write(frame.data(), frame.size());
}