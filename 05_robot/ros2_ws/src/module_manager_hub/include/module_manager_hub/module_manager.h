#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <string>
#include <array>
#include <vector>
#include <atomic>
#include <yaml-cpp/yaml.h>

#include <boost/asio.hpp>
#include <boost/array.hpp>

#include "module.h"
#include "module_manager_hub/msg/module_status.hpp"
#include "module_manager_hub/srv/module_control.hpp"
#include "module_manager_hub/msg/robotarmcontrol.hpp"
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

using boost::asio::buffer;

// 二进制串口协议常量（32字节固定帧）
constexpr uint8_t  SERIAL_SOF0      = 0xAA;
constexpr uint8_t  SERIAL_SOF1      = 0x55;
constexpr size_t   SERIAL_HEADER_LEN = 4;   // SOF0 + SOF1 + CmdType + PayLen
constexpr size_t   SERIAL_DATA_LEN   = 16;  // 数据域固定 16 字节
constexpr size_t   SERIAL_RESERVED   = 10;  // 保留 10 字节
constexpr size_t   SERIAL_CRC_LEN    = 2;   // CRC16 2 字节
constexpr size_t   SERIAL_FRAME_LEN  = SERIAL_HEADER_LEN + SERIAL_DATA_LEN + SERIAL_RESERVED + SERIAL_CRC_LEN;  // = 32
constexpr size_t   SERIAL_BUF_SIZE   = SERIAL_FRAME_LEN * 4;

// 指令类型
constexpr uint8_t CMD_MOVE    = 0x01;  // 速度指令
constexpr uint8_t CMD_TASK    = 0x02;  // 任务指令
constexpr uint8_t CMD_HEARTBEAT = 0x03; // 心跳包
constexpr uint8_t CMD_STATUS  = 0x04;  // 状态反馈

struct CmdRoute {
  std::string topic;
  std::string msg_type;
};

class ModuleManager : public rclcpp::Node {
public:
  explicit ModuleManager(const std::string &name);
  ~ModuleManager() override;

private:
  void loadConfig(const YAML::Node &mods);
  LaunchType parseLaunchType(const std::string &type_str);

  // 模块生命周期
  bool startModule(const std::string &name);
  bool stopModule(const std::string &name);
  bool restartModule(const std::string &name);

  // 子进程管理
  int execCommand(const std::string &cmd, const std::string &work_dir, int &out_pid);
  bool killProcess(int pid);
  bool isProcessAlive(int pid);

  void monitorTimerCallback();
  void publishModuleStatus();

  void moduleControlCallback(
    const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
    std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res);

  // 脚本任务
  void loadScriptTasks(const YAML::Node &tasks_node);
  void execScriptTask(const std::string &name);

  // ---- 数据成员 ----
  std::map<std::string, Module> modules_;
  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> topic_subs_;
  std::map<std::string, ScriptTask> script_tasks_;

  rclcpp::Publisher<module_manager_hub::msg::ModuleStatus>::SharedPtr status_pub_;
  rclcpp::Service<module_manager_hub::srv::ModuleControl>::SharedPtr ctrl_srv_;
  rclcpp::TimerBase::SharedPtr monitor_timer_;

  // ========== 串口通信（替代 UDP） ==========
  struct SerialConfig {
    std::string port = "/dev/ttyUSB0";
    int baud_rate = 115200;
  };
  SerialConfig serial_cfg_;

  void initSerial();
  void doSerialRead();
  void parseSerialPacket(const uint8_t *payload, size_t pay_len, uint8_t cmd_type);
  void sendSerialResponse(uint8_t cmd_type, const uint8_t *payload, size_t payload_len);
  void sendSerialStatus();  // 定期向遥控端回发模块状态
  static uint16_t calcCRC16(const uint8_t *data, size_t len);
  void buildSerialFrame(uint8_t cmd_type, const uint8_t *payload, size_t payload_len,
                        std::vector<uint8_t> &out_frame);

  boost::asio::io_context io_context_;
  boost::asio::serial_port serial_port_;
  std::thread io_thread_;  // io_context 运行线程
  std::array<uint8_t, SERIAL_BUF_SIZE> serial_rx_buf_;
  std::vector<uint8_t> serial_rx_frame_;  // 帧缓存（跨多次读取拼接）
  uint8_t serial_rx_frame_end_marker_[2] = {SERIAL_SOF0, SERIAL_SOF1};  // 用于查找帧头

  // 指令路由
  void loadCmdRoute(const YAML::Node &route_node);
  void dispatchCommand(const std::string &cmd, const std::vector<double> &params);
  std::map<std::string, CmdRoute> cmd_routes_;
  std::map<std::string, rclcpp::Publisher<module_manager_hub::msg::Robotarmcontrol>::SharedPtr> arm_control_pubs_;

  // 遥控话题
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr control_mode_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr hw_switch_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_cmd_pub_;

  std::string current_mode_;
  bool hw_switch_state_;

  // ---- 防误触（按键防抖） ----
  static constexpr double TASK_CMD_DEBOUNCE_SEC = 2.0;  // 数字键命令2秒内只响应第一个
  double last_task_cmd_time_ = 0.0;

  // ========== 速度插值（50Hz 平滑输出 cmd_vel） ==========
  static constexpr double SPEED_INTERPOLATE_HZ = 50.0;   // 输出频率
  static constexpr double LINEAR_ACCEL  = 0.2;            // 线加速度 m/s²
  static constexpr double ANGULAR_ACCEL = 0.3;            // 角加速度 rad/s²
  static constexpr double LINEAR_MAX    = 0.6;            // 线速度上限 m/s
  static constexpr double ANGULAR_MAX   = 1.0;            // 角速度上限 rad/s

  void interpolateSpeedLoop();

  rclcpp::TimerBase::SharedPtr speed_timer_;
  std::thread speed_thread_;
  std::atomic<bool> speed_running_{false};

  // 目标速度（每次收到串口控制指令更新，原子变量跨线程安全）
  std::atomic<float> target_linear_{0.0f};
  std::atomic<float> target_angular_{0.0f};

  // 当前实际输出速度（插值逐步逼近 target）
  float current_linear_  = 0.0f;
  float current_angular_ = 0.0f;
};