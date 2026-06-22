#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <string>
#include <vector>
#include <atomic>
#include <array>
#include <cstdint>
#include <yaml-cpp/yaml.h>

#include "module.h"
#include "module_manager_hub/serial_reader.h"
#include "module_manager_hub/msg/robotarmcontrol.hpp"
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

// 32 字节固定帧串口协议常量（机器人遥控协议）
constexpr uint8_t  PROTO_SOF0       = 0xAA;
constexpr uint8_t  PROTO_SOF1       = 0x55;
constexpr size_t   PROTO_HEADER_LEN = 4;    // SOF0 + SOF1 + CmdType + PayLen
constexpr size_t   PROTO_DATA_LEN   = 16;
constexpr size_t   PROTO_RESERVED   = 10;
constexpr size_t   PROTO_FRAME_LEN  = 32;

constexpr uint8_t CMD_MOVE       = 0x01;
constexpr uint8_t CMD_TASK       = 0x02;
constexpr uint8_t CMD_HEARTBEAT  = 0x03;
constexpr uint8_t CMD_STATUS     = 0x04;

struct CmdRoute {
  std::string topic;
  std::string msg_type;
};

/// 模块管理器 —— 串口遥控 ↔ ROS 桥接
/// 协议解析在 node 内部完成，SerialReader 仅提供裸字节收发
class ModuleManager : public rclcpp::Node {
public:
  explicit ModuleManager(const std::string &name,
                         const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~ModuleManager() override;

private:
  // 从 YAML 加载配置
  void loadSerialConfig(const YAML::Node &root);
  void loadTrackedModules(const YAML::Node &mods);
  void loadCmdRoute(const YAML::Node &route_node);
  void dispatchCommand(const std::string &cmd, const std::vector<double> &params);

  // ========== 32 字节固定帧协议解析（内联在 ModuleManager） ==========
  void onSerialData(const uint8_t *data, size_t len);  // SerialReader 裸数据回调
  void parseSerialPacket(uint8_t cmd_type, const uint8_t *payload, size_t pay_len);
  void sendSerialStatus();
  void buildSerialFrame(uint8_t cmd_type, const uint8_t *payload, size_t payload_len,
                        std::vector<uint8_t> &out_frame);

  SerialReader serial_reader_;
  std::vector<uint8_t> rx_frame_;
  uint8_t sof_marker_[2] = {PROTO_SOF0, PROTO_SOF1};

  // ========== 模块状态追踪（仅通过话题心跳，不管理进程） ==========
  std::map<std::string, TrackedModule> tracked_modules_;
  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> watch_subs_;
  rclcpp::TimerBase::SharedPtr status_timer_;  // 500ms 定期推送串口状态

  // ========== 指令路由 ==========
  std::map<std::string, CmdRoute> cmd_routes_;
  std::map<std::string, rclcpp::Publisher<module_manager_hub::msg::Robotarmcontrol>::SharedPtr> arm_control_pubs_;

  // ========== 遥控话题 ==========
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr control_mode_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr hw_switch_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_cmd_pub_;

  std::string current_mode_;
  bool hw_switch_state_;

  // ---- 防误触 ----
  static constexpr double TASK_CMD_DEBOUNCE_SEC = 2.0;
  double last_task_cmd_time_ = 0.0;

  // ========== 速度插值（80Hz 平滑输出 cmd_vel） ==========
  static constexpr double SPEED_INTERPOLATE_HZ = 80.0;
  static constexpr double LINEAR_ACCEL  = 0.2;
  static constexpr double ANGULAR_ACCEL = 0.3;
  static constexpr double LINEAR_MAX    = 0.6;
  static constexpr double ANGULAR_MAX   = 1.0;

  void interpolateSpeedLoop();

  std::thread speed_thread_;
  std::atomic<bool> speed_running_{false};

  std::atomic<float> target_linear_{0.0f};
  std::atomic<float> target_angular_{0.0f};

  float current_linear_  = 0.0f;
  float current_angular_ = 0.0f;
};