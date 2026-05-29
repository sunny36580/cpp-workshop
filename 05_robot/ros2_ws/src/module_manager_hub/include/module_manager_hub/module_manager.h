#pragma once
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <string>
#include <array>
#include <yaml-cpp/yaml.h>

// 正确的 UDP 头文件（boost::asio）
#include <boost/asio.hpp>
#include <boost/array.hpp>

#include "module.h"
#include "module_manager_hub/msg/module_status.hpp"
#include "module_manager_hub/srv/module_control.hpp"
#include "module_manager_hub/msg/robotarmcontrol.hpp"
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/string.hpp>
#include <std_msgs/msg/bool.hpp>

using boost::asio::ip::udp;
using boost::asio::buffer;
using boost::array;

// 指令路由结构体
struct CmdRoute
{
  std::string topic;
  std::string msg_type;
};

class ModuleManager : public rclcpp::Node {
public:
  explicit ModuleManager(const std::string &name);
  ~ModuleManager() override;

private:
  void loadConfig(const std::string &path);
  bool startModule(const std::string &name);
  bool stopModule(const std::string &name);
  bool restartModule(const std::string &name);

  // 通用话题回调：任何话题有数据就更新时间
  template <typename MsgT>
  void topicCallback(const std::shared_ptr<MsgT>, const std::string &mod_name);

  void monitorTimerCallback();
  void publishModuleStatus();

  void moduleControlCallback(
    const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
    std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res
  );

  std::map<std::string, Module> modules_;
  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> topic_subs_;

  rclcpp::Publisher<module_manager_hub::msg::ModuleStatus>::SharedPtr status_pub_;
  rclcpp::Service<module_manager_hub::srv::ModuleControl>::SharedPtr ctrl_srv_;
  rclcpp::TimerBase::SharedPtr monitor_timer_;

  // ========== UDP 服务 ==========
  void initUdpServer();
  void doReceive();
  void parseUdpCommand(const std::string &data);

  boost::asio::io_context io_context_;
  udp::socket udp_socket_;
  udp::endpoint remote_endpoint_;
  std::array<char, 1024> recv_buffer_;

  // ========== 指令路由 + 消息分发 ==========
  void loadCmdRoute(const YAML::Node &route_node);
  void dispatchCommand(const std::string &cmd, const std::vector<double> &params);

  std::map<std::string, CmdRoute> cmd_routes_;

  // ========== 【仅保留自定义消息发布器】 ==========
  std::map<std::string, rclcpp::Publisher<module_manager_hub::msg::Robotarmcontrol>::SharedPtr> arm_control_pubs_;

  // ========== 遥控制相关（UDP → ROS 话题） ==========
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr control_mode_pub_;
  rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr hw_switch_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_cmd_pub_;

  std::string current_mode_;
  bool hw_switch_state_;
};