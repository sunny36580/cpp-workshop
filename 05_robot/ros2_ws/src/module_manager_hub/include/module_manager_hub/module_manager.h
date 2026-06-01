#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <string>
#include <array>
#include <vector>
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

// 二进制串口协议常量
constexpr uint8_t SERIAL_SOF       = 0xAA;
constexpr size_t  SERIAL_HEADER_LEN = 3;  // SOF + CmdType + PayLen
constexpr size_t  SERIAL_CHECKSUM_LEN = 1;
constexpr size_t  SERIAL_BUF_SIZE  = 256;

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
  static uint8_t calcChecksum(const uint8_t *data, size_t len);

  boost::asio::io_context io_context_;
  boost::asio::serial_port serial_port_;
  std::array<uint8_t, SERIAL_BUF_SIZE> serial_rx_buf_;
  std::vector<uint8_t> serial_rx_frame_;  // 帧缓存（跨多次读取拼接）

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
};