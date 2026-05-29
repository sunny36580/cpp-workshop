#include "module_manager_hub/module_manager.h"
#include <chrono>
#include <cstdlib>
#include <thread>
#include <sstream>
#include <vector>

using namespace std::chrono_literals;
using boost::asio::ip::udp;

ModuleManager::ModuleManager(const std::string &name)
  : Node(name), udp_socket_(io_context_), current_mode_("STAND"), hw_switch_state_(false)
{
  // 模块状态发布
  status_pub_ = this->create_publisher<module_manager_hub::msg::ModuleStatus>("/robot/modules_status", 10);

  // 模块控制服务
  ctrl_srv_ = this->create_service<module_manager_hub::srv::ModuleControl>(
    "/robot/module_control",
    std::bind(&ModuleManager::moduleControlCallback, this, std::placeholders::_1, std::placeholders::_2)
  );

  // 加载主配置
  std::string config_path = "config/modules.yaml";
  this->declare_parameter<std::string>("config_path", config_path);
  this->get_parameter("config_path", config_path);
  YAML::Node root_cfg = YAML::LoadFile(config_path);

  // 1. 加载模块配置
  loadConfig(config_path);
  // 2. 加载指令路由配置
  if (root_cfg["cmd_route"])
  {
    loadCmdRoute(root_cfg["cmd_route"]);
  }

  // 模块监控定时器
  monitor_timer_ = this->create_wall_timer(500ms, std::bind(&ModuleManager::monitorTimerCallback, this));

  // ========== 远程控制话题发布器 ==========
  cmd_vel_pub_ = this->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
  control_mode_pub_ = this->create_publisher<std_msgs::msg::String>("/control_mode", 10);
  hw_switch_pub_ = this->create_publisher<std_msgs::msg::Bool>("/hwswitch", 10);
  action_cmd_pub_ = this->create_publisher<std_msgs::msg::String>("/action_cmd", 10);

  // 启动UDP服务
  initUdpServer();
  std::thread([this]() { io_context_.run(); }).detach();

  RCLCPP_INFO(this->get_logger(), "人形机器人中枢启动完成 ✅");
}

ModuleManager::~ModuleManager()
{
  io_context_.stop();
}

// 加载指令路由表 & 预创建【仅自定义消息】发布器
void ModuleManager::loadCmdRoute(const YAML::Node &route_node)
{
  for (const auto &entry : route_node)
  {
    std::string cmd_name = entry.first.as<std::string>();
    CmdRoute route;
    route.topic = entry.second["topic"].as<std::string>();
    route.msg_type = entry.second["msg_type"].as<std::string>();
    cmd_routes_[cmd_name] = route;

    // 仅创建自定义 Robotarmcontrol 发布器
    if (route.msg_type == "Robotarmcontrol")
    {
      arm_control_pubs_[route.topic] =
        this->create_publisher<module_manager_hub::msg::Robotarmcontrol>(route.topic, 10);
    }

    RCLCPP_INFO(this->get_logger(), "指令路由: %s → %s [%s]",
                cmd_name.c_str(), route.topic.c_str(), route.msg_type.c_str());
  }
}

// UDP 初始化
void ModuleManager::initUdpServer()
{
  try
  {
    udp_socket_.open(udp::v4());
    udp_socket_.bind(udp::endpoint(udp::v4(), 8888));
    doReceive();
    RCLCPP_INFO(this->get_logger(), "UDP 监听端口: 8888");
  }
  catch (std::exception &e)
  {
    RCLCPP_ERROR(this->get_logger(), "UDP 启动失败: %s", e.what());
  }
}

// UDP 异步接收
void ModuleManager::doReceive()
{
  udp_socket_.async_receive_from(
    buffer(recv_buffer_),
    remote_endpoint_,
    [this](std::error_code ec, std::size_t bytes)
    {
      if (!ec && bytes > 0)
      {
        std::string data(recv_buffer_.data(), bytes);
        parseUdpCommand(data);
      }
      doReceive();
    });
}

// 解析UDP字符串指令 格式: 指令名 参数1 参数2 参数3 ...
void ModuleManager::parseUdpCommand(const std::string &data) {
  // 去除首尾空白和换行
  std::string cmd = data;
  cmd.erase(0, cmd.find_first_not_of(" \t\n\r"));
  cmd.erase(cmd.find_last_not_of(" \t\n\r") + 1);

  RCLCPP_INFO(this->get_logger(), "收到UDP指令: %s", cmd.c_str());

  // 按逗号分割
  std::vector<std::string> parts;
  std::stringstream ss(cmd);
  std::string part;
  while (std::getline(ss, part, ',')) {
    parts.push_back(part);
  }

  if (parts.empty()) {
    RCLCPP_WARN(this->get_logger(), "空指令，忽略");
    return;
  }

  // 解析cmd_type
  int cmd_type;
  try {
    cmd_type = std::stoi(parts[0]);
  } catch (...) {
    RCLCPP_WARN(this->get_logger(), "无效的指令类型: %s", parts[0].c_str());
    return;
  }

  // 发布当前模式（每次UDP指令都发布，方便下游感知状态变化）
  auto publish_mode = [this]() {
    std_msgs::msg::String mode_msg;
    mode_msg.data = current_mode_;
    control_mode_pub_->publish(mode_msg);
  };

  switch (cmd_type) {
    // ------------------------------
    // cmd_type=1: 运动指令
    // 格式: 1,linear,angular
    // 对应: WASD → vx/vy（前后/左右平移），angular = wz（转向）
    // ------------------------------
    case 1: {
      if (parts.size() < 3) {
        RCLCPP_WARN(this->get_logger(), "运动指令参数不足，需要: 1,linear,angular");
        return;
      }
      try {
        double linear = std::stod(parts[1]);
        double angular = std::stod(parts[2]);

        // 构建 Twist 消息
        geometry_msgs::msg::Twist twist_msg;
        twist_msg.linear.x = linear;     // 前后: W=正, S=负
        twist_msg.angular.z = angular;   // 转向: A=正(左转), D=负(右转)
        // 保持 y 轴移动为 0（四足/双足机器人通常无侧移）
        twist_msg.linear.y = 0.0;

        cmd_vel_pub_->publish(twist_msg);

        RCLCPP_DEBUG(this->get_logger(), "发布运动指令: 线速度=%.2f, 角速度=%.2f", linear, angular);

        // 如果有非零速度，自动切换到行走模式（如果当前是 STAND）
        if ((std::abs(linear) > 0.001 || std::abs(angular) > 0.001) &&
            current_mode_ == "STAND") {
          current_mode_ = "WALK_FULL";
          RCLCPP_INFO(this->get_logger(), "检测到运动输入，自动切换到: WALK_FULL");
          publish_mode();
        }
      } catch (...) {
        RCLCPP_WARN(this->get_logger(), "运动指令参数解析失败");
      }
      break;
    }

    // ------------------------------
    // cmd_type=2: 任务指令
    // 格式: 2,task_id
    // ------------------------------
    case 2: {
      if (parts.size() < 2) {
        RCLCPP_WARN(this->get_logger(), "任务指令参数不足，需要: 2,task_id");
        return;
      }
      try {
        int task_id = std::stoi(parts[1]);
        RCLCPP_INFO(this->get_logger(), "转发任务指令: 执行任务 %d", task_id);

        // 将预设任务映射为 action_cmd 字符串
        // 与 remote_control_client.py 中的 TASK_LIST 保持一致
        std::string action_name;
        switch (task_id) {
          case 1:  action_name = "WAVE";        break;  // 挥手
          case 2:  action_name = "HANDSHAKE";   break;  // 握手
          case 3:  action_name = "PERFORM";     break;  // 表演
          case 4:  action_name = "TURN_180";    break;  // 原地旋转
          case 5:  action_name = "FINGER_SHOW"; break;  // 手指动作
          case 6:  action_name = "WAVE";        break;  // 挥手（复用）
          case 7:  action_name = "SMILE";       break;  // 表情
          case 8:  action_name = "STAND";       break;  // 回到待机
          default: action_name = "NONE";        break;
        }

        if (action_name != "NONE") {
          std_msgs::msg::String action_msg;
          action_msg.data = action_name;
          action_cmd_pub_->publish(action_msg);
          RCLCPP_INFO(this->get_logger(), "发布动作指令: %s", action_name.c_str());
        }
      } catch (...) {
        RCLCPP_WARN(this->get_logger(), "任务指令参数解析失败");
      }
      break;
    }

    // ------------------------------
    // cmd_type=3: 急停指令
    // 格式: 3
    // ------------------------------
    case 3: {
      RCLCPP_WARN(this->get_logger(), ">>> 收到急停指令 <<<");

      // 1. 立即发布零速 Twist
      geometry_msgs::msg::Twist stop_twist;
      cmd_vel_pub_->publish(stop_twist);

      // 2. 切换模式为 EMERGENCY
      current_mode_ = "EMERGENCY";
      publish_mode();

      // 3. 断开硬件使能
      hw_switch_state_ = false;
      std_msgs::msg::Bool hw_msg;
      hw_msg.data = false;
      hw_switch_pub_->publish(hw_msg);

      RCLCPP_WARN(this->get_logger(), "急停已执行: 零速+EMERGENCY模式+硬件断开");
      break;
    }

    default:
      RCLCPP_WARN(this->get_logger(), "未知指令类型: %d", cmd_type);
      break;
  }
}

// 【核心】仅转发你的自定义消息
void ModuleManager::dispatchCommand(const std::string &cmd, const std::vector<double> &params)
{
  auto route_it = cmd_routes_.find(cmd);
  if (route_it == cmd_routes_.end())
  {
    RCLCPP_WARN(this->get_logger(), "未知指令: %s", cmd.c_str());
    return;
  }

  const CmdRoute &route = route_it->second;
  const std::string &topic = route.topic;

  // 仅处理自定义 Robotarmcontrol 消息
  if (route.msg_type == "Robotarmcontrol")
  {
    auto pub_it = arm_control_pubs_.find(topic);
    if (pub_it == arm_control_pubs_.end())
    {
      RCLCPP_WARN(this->get_logger(), "发布器未找到: %s", topic.c_str());
      return;
    }

    module_manager_hub::msg::Robotarmcontrol msg;

    // UDP 格式: arm_control id1 pos1 vel1 id2 pos2 vel2 ...
    for (size_t i = 0; i + 2 < params.size(); i += 3)
    {
      msg.motor_ids.push_back(static_cast<uint32_t>(params[i]));
      msg.target_positions.push_back(static_cast<float>(params[i + 1]));
      msg.target_velocitie.push_back(static_cast<float>(params[i + 2]));
    }

    pub_it->second->publish(msg);
    RCLCPP_INFO(this->get_logger(), "转发机械臂指令 ✅ 电机数=%zu",
                msg.motor_ids.size());
  }
  else
  {
    RCLCPP_WARN(this->get_logger(), "不支持的消息类型: %s", route.msg_type.c_str());
  }
}

// ===================== 以下原有模块管理代码 完全不变 =====================
void ModuleManager::loadConfig(const std::string &path)
{
  try
  {
    YAML::Node config = YAML::LoadFile(path);
    for (const auto &entry : config["modules"])
    {
      Module m;
      m.name = entry.first.as<std::string>();
      m.node_name = entry.second["node_name"].as<std::string>();
      m.watch_topic = entry.second["watch_topic"].as<std::string>();
      m.enabled = entry.second["enabled"].as<bool>();
      m.auto_start = entry.second["auto_start"].as<bool>();
      m.auto_restart = entry.second["auto_restart"].as<bool>();
      m.timeout_sec = entry.second["timeout_sec"].as<double>();
      m.last_msg_time = this->now().seconds();

      modules_[m.name] = m;

      auto cb = [this, name = m.name](const std::shared_ptr<rclcpp::SerializedMessage> msg)
      {
        this->topicCallback(msg, name);
      };
      auto sub = this->create_generic_subscription(
        m.watch_topic, "rosidl_typesupport_cpp", 10, cb);
      topic_subs_[m.name] = sub;

      if (m.enabled && m.auto_start)
        startModule(m.name);
    }
  }
  catch (std::exception &e)
  {
    RCLCPP_ERROR(this->get_logger(), "模块配置加载失败: %s", e.what());
  }
}

template <typename MsgT>
void ModuleManager::topicCallback(const std::shared_ptr<MsgT>, const std::string &mod_name)
{
  if (modules_.count(mod_name))
  {
    modules_[mod_name].last_msg_time = this->now().seconds();
    modules_[mod_name].online = true;
    modules_[mod_name].running = true;
  }
}

void ModuleManager::monitorTimerCallback()
{
  double now = this->now().seconds();
  const double restart_interval = 3.0;
  static std::map<std::string, double> last_restart_time;

  for (auto &pair : modules_)
  {
    auto &m = pair.second;
    if (!m.enabled) continue;

    if (now - m.last_msg_time > m.timeout_sec)
    {
      m.online = false;
      m.running = false;

      if (m.auto_restart)
      {
        auto it = last_restart_time.find(pair.first);
        if (it == last_restart_time.end() || now - it->second > restart_interval)
        {
          RCLCPP_WARN(this->get_logger(), "模块 %s 超时，执行自动重启", pair.first.c_str());
          restartModule(pair.first);
          last_restart_time[pair.first] = now;
        }
      }
    }
  }
  publishModuleStatus();
}

bool ModuleManager::startModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  auto &m = modules_[name];
  std::string cmd = "ros2 run module_manager_hub " + m.node_name + " &";
  system(cmd.c_str());
  m.running = true;
  return true;
}

bool ModuleManager::stopModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  std::string cmd = "pkill -f " + modules_[name].node_name;
  system(cmd.c_str());
  modules_[name].online = false;
  modules_[name].running = false;
  return true;
}

bool ModuleManager::restartModule(const std::string &name)
{
  stopModule(name);
  rclcpp::sleep_for(1s);
  return startModule(name);
}

void ModuleManager::moduleControlCallback(
  const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
  std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res)
{
  res->success = false;
  if (!modules_.count(req->module_name))
  {
    res->message = "模块不存在";
    return;
  }

  switch (req->cmd)
  {
    case 1:
      res->success = startModule(req->module_name);
      res->message = "已启动";
      break;
    case 2:
      res->success = stopModule(req->module_name);
      res->message = "已停止";
      break;
    case 3:
      res->success = restartModule(req->module_name);
      res->message = "已重启";
      break;
    case 4:
      modules_[req->module_name].enabled = false;
      res->success = true;
      res->message = "已禁用";
      break;
    default:
      res->message = "指令无效";
      break;
  }
}

void ModuleManager::publishModuleStatus()
{
  for (auto &pair : modules_)
  {
    auto &m = pair.second;
    module_manager_hub::msg::ModuleStatus msg;
    msg.name = m.name;
    msg.online = m.online;
    msg.running = m.running;
    msg.last_heartbeat = m.last_msg_time;
    msg.state_msg = m.online ? "RUNNING" : "LOST";
    status_pub_->publish(msg);
  }
}