#include "module_manager_hub/module_manager.h"
#include <chrono>
#include <cstdlib>
#include <thread>
#include <sstream>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

using namespace std::chrono_literals;
using boost::asio::ip::udp;

// =====================================================================
// 构造函数 & 析构
// =====================================================================
ModuleManager::ModuleManager(const std::string &name)
  : Node(name), udp_socket_(io_context_), current_mode_("STAND"), hw_switch_state_(false)
{
  // 安装 SIGCHLD 信号处理，自动回收僵尸进程
  struct sigaction sa;
  sa.sa_handler = SIG_IGN;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_NOCLDWAIT;
  sigaction(SIGCHLD, &sa, nullptr);

  status_pub_ = this->create_publisher<module_manager_hub::msg::ModuleStatus>("/robot/modules_status", 10);

  ctrl_srv_ = this->create_service<module_manager_hub::srv::ModuleControl>(
    "/robot/module_control",
    std::bind(&ModuleManager::moduleControlCallback, this, std::placeholders::_1, std::placeholders::_2));

  // 加载配置
  std::string config_path = "config/modules.yaml";
  this->declare_parameter<std::string>("config_path", config_path);
  this->get_parameter("config_path", config_path);
  YAML::Node root_cfg = YAML::LoadFile(config_path);

  if (root_cfg["modules"])      loadConfig(root_cfg["modules"]);
  if (root_cfg["cmd_route"])    loadCmdRoute(root_cfg["cmd_route"]);
  if (root_cfg["script_tasks"]) loadScriptTasks(root_cfg["script_tasks"]);

  monitor_timer_ = this->create_wall_timer(500ms, std::bind(&ModuleManager::monitorTimerCallback, this));

  // 遥控话题
  cmd_vel_pub_    = this->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
  control_mode_pub_ = this->create_publisher<std_msgs::msg::String>("/control_mode", 10);
  hw_switch_pub_  = this->create_publisher<std_msgs::msg::Bool>("/hwswitch", 10);
  action_cmd_pub_ = this->create_publisher<std_msgs::msg::String>("/action_cmd", 10);

  initUdpServer();
  std::thread([this]() { io_context_.run(); }).detach();

  RCLCPP_INFO(this->get_logger(), "人形机器人中枢启动完成 ✅");
}

ModuleManager::~ModuleManager()
{
  io_context_.stop();
  // 给 io_context 线程一点时间退出，避免访问已销毁的对象
  std::this_thread::sleep_for(100ms);

  // 清理子进程
  for (auto &p : modules_)
    if (p.second.pid > 0) killProcess(p.second.pid);
}

// =====================================================================
// 配置加载
// =====================================================================
LaunchType ModuleManager::parseLaunchType(const std::string &s)
{
  if (s == "ros2_launch") return LaunchType::ROS2_LAUNCH;
  return LaunchType::ROS2_RUN;
}

void ModuleManager::loadConfig(const YAML::Node &mods)
{
  if (!mods) return;

  for (const auto &entry : mods) {
    try {
      Module m;
      m.name        = entry.first.as<std::string>();
      auto &cfg     = entry.second;
      m.launch_type = parseLaunchType(cfg["launch_type"] ? cfg["launch_type"].as<std::string>() : "ros2_run");
      m.package_name = cfg["package"] ? cfg["package"].as<std::string>() : "module_manager_hub";
      m.node_name   = cfg["node_name"] ? cfg["node_name"].as<std::string>() : "";
      m.watch_topic = cfg["watch_topic"] ? cfg["watch_topic"].as<std::string>() : "";
      m.watch_type  = cfg["watch_type"] ? cfg["watch_type"].as<std::string>() : "";
      m.working_dir = cfg["working_dir"] ? cfg["working_dir"].as<std::string>() : "";
      m.enabled     = cfg["enabled"] ? cfg["enabled"].as<bool>() : false;
      m.auto_start  = cfg["auto_start"] ? cfg["auto_start"].as<bool>() : false;
      m.auto_restart = cfg["auto_restart"] ? cfg["auto_restart"].as<bool>() : false;
      m.timeout_sec = cfg["timeout_sec"] ? cfg["timeout_sec"].as<double>() : 5.0;
      m.last_msg_time = this->now().seconds();
      modules_[m.name] = m;

      std::string lt = (m.launch_type == LaunchType::ROS2_LAUNCH) ? "LAUNCH" : "RUN";
      RCLCPP_INFO(this->get_logger(), 
        "[MODULE] %-16s | %-6s | %-20s | %-30s | E:%d A:%d R:%d | T:%.1fs| WD: %-30s",
        m.name.c_str(),
        lt.c_str(),
        m.package_name.c_str(),
        m.node_name.c_str(),
        m.enabled,
        m.auto_start,
        m.auto_restart,
        m.timeout_sec,
        m.working_dir.c_str()
      );

      if (m.enabled && m.auto_start) startModule(m.name);

      // 监控订阅（单独 try-catch，不阻塞模块启动）
      if (!m.watch_topic.empty()) {
        try {
          auto cb = [this, n = m.name](const std::shared_ptr<rclcpp::SerializedMessage>) {
            if (modules_.count(n)) {
              modules_[n].last_msg_time = this->now().seconds();
              modules_[n].online = true;
              modules_[n].running = true;
            }
          };
          std::string type_str = m.watch_type.empty() ? "std_msgs/msg/String" : m.watch_type;
          topic_subs_[m.name] = this->create_generic_subscription(m.watch_topic, type_str, 10, cb);
          RCLCPP_INFO(this->get_logger(), "监控模块 %s → %s [%s]", m.name.c_str(), m.watch_topic.c_str(), type_str.c_str());
        } catch (std::exception &e) {
          RCLCPP_WARN(this->get_logger(), "监控订阅失败 %s: %s", m.watch_topic.c_str(), e.what());
        }
      }
    } catch (std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "跳过无效模块配置: %s", e.what());
    }
  }
}

void ModuleManager::loadScriptTasks(const YAML::Node &tasks_node)
{
  for (const auto &entry : tasks_node) {
    ScriptTask t;
    t.name        = entry.first.as<std::string>();
    t.description = entry.second["description"] ? entry.second["description"].as<std::string>() : "";
    t.script_path = entry.second["script"].as<std::string>();
    t.enabled     = entry.second["enabled"] ? entry.second["enabled"].as<bool>() : true;
    script_tasks_[t.name] = t;
    RCLCPP_INFO(this->get_logger(), "脚本任务注册: %s → %s", t.name.c_str(), t.script_path.c_str());
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
// UDP
// =====================================================================
void ModuleManager::initUdpServer()
{
  try {
    udp_socket_.open(udp::v4());
    udp_socket_.bind(udp::endpoint(udp::v4(), 8888));
    doReceive();
    RCLCPP_INFO(this->get_logger(), "UDP 监听端口: 8888");
  } catch (std::exception &e) {
    RCLCPP_ERROR(this->get_logger(), "UDP 启动失败: %s", e.what());
  }
}

void ModuleManager::doReceive()
{
  udp_socket_.async_receive_from(buffer(recv_buffer_), remote_endpoint_,
    [this](std::error_code ec, std::size_t bytes) {
      if (!ec && bytes > 0) parseUdpCommand(std::string(recv_buffer_.data(), bytes));
      doReceive();
    });
}

void ModuleManager::parseUdpCommand(const std::string &data)
{
  std::string cmd = data;
  cmd.erase(0, cmd.find_first_not_of(" \t\n\r"));
  cmd.erase(cmd.find_last_not_of(" \t\n\r") + 1);
  RCLCPP_INFO(this->get_logger(), "收到UDP指令: %s", cmd.c_str());

  std::vector<std::string> parts;
  std::stringstream ss(cmd);
  std::string part;
  while (std::getline(ss, part, ',')) parts.push_back(part);
  if (parts.empty()) return;

  int cmd_type;
  try { cmd_type = std::stoi(parts[0]); }
  catch (...) { RCLCPP_WARN(this->get_logger(), "无效指令类型"); return; }

  auto publish_mode = [this]() {
    std_msgs::msg::String m; m.data = current_mode_; control_mode_pub_->publish(m);
  };

  switch (cmd_type)
  {
    // ---- cmd_type=1: 运动指令 ----
    case 1: {
      if (parts.size() < 3) return;
      try {
        double linear = std::stod(parts[1]), angular = std::stod(parts[2]);
        geometry_msgs::msg::Twist t;
        t.linear.x = linear; t.linear.y = 0; t.angular.z = angular;
        cmd_vel_pub_->publish(t);
        if ((std::abs(linear) > 0.001 || std::abs(angular) > 0.001) && current_mode_ == "STAND") {
          current_mode_ = "WALK_FULL";
          publish_mode();
        }
      } catch (...) {}
      break;
    }

    // ---- cmd_type=2: 任务指令 ----
    case 2: {
      if (parts.size() < 2) return;
      int task_id = std::stoi(parts[1]);

      // 先发 action_cmd 话题
      static const char* actions[] = {"WAVE","HANDSHAKE","PERFORM","TURN_180","FINGER_SHOW","WAVE","SMILE","STAND"};
      if (task_id >= 1 && task_id <= 8) {
        std_msgs::msg::String a;
        a.data = actions[task_id-1];
        action_cmd_pub_->publish(a);
        RCLCPP_INFO(this->get_logger(), "转发 action_cmd: %s", a.data.c_str());
      }

      // 如果有同名脚本任务，一并执行
      std::string task_name = "task_" + std::to_string(task_id);
      if (script_tasks_.count(task_name) && script_tasks_[task_name].enabled) {
        execScriptTask(task_name);
      }
      break;
    }

    // ---- cmd_type=3: 急停 ----
    case 3: {
      RCLCPP_WARN(this->get_logger(), ">>> 急停 <<<");
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
// 子进程管理
// =====================================================================
int ModuleManager::execCommand(const std::string &cmd, const std::string &work_dir, int &out_pid)
{
  int pid = fork();
  if (pid < 0) { RCLCPP_ERROR(this->get_logger(), "fork 失败"); return -1; }
  if (pid == 0) {
    if (!work_dir.empty()) chdir(work_dir.c_str());

    // 如果有工作目录且存在 install/setup.bash，自动 source
    std::string full_cmd;
    std::string setup_script = work_dir + "/install/setup.bash";
    if (!work_dir.empty() && access(setup_script.c_str(), F_OK) == 0) {
      full_cmd = "source " + setup_script + " && " + cmd;
    } else {
      full_cmd = cmd;
    }

    // 子进程 stdout/stderr 透传给父进程以便排查
    setpgid(0, 0);
    setsid();
    execl("/bin/sh", "sh", "-c", full_cmd.c_str(), (char*)nullptr);
    _exit(127);
  }
  out_pid = pid;
  RCLCPP_INFO(this->get_logger(), "启动子进程 PID=%d: %s", pid, cmd.c_str());
  return pid;
}

bool ModuleManager::killProcess(int pid)
{
  if (pid <= 0) return false;
  kill(-pid, SIGTERM);
  for (int i = 0; i < 10; i++) {
    if (waitpid(-pid, nullptr, WNOHANG) > 0) return true;
    std::this_thread::sleep_for(500ms);
  }
  kill(-pid, SIGKILL);
  waitpid(-pid, nullptr, 0);
  return true;
}

bool ModuleManager::isProcessAlive(int pid)
{
  return pid > 0 && kill(pid, 0) == 0;
}

// =====================================================================
// 模块生命周期
// =====================================================================
bool ModuleManager::startModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  auto &m = modules_[name];
  if (m.pid > 0 && isProcessAlive(m.pid)) return true; // 已运行

  std::string cmd;
  if (m.launch_type == LaunchType::ROS2_LAUNCH)
    cmd = "ros2 launch " + m.package_name + " " + m.node_name;
  else
    cmd = "ros2 run " + m.package_name + " " + m.node_name;

  // 日志：工作目录 & 自动 source 状态
  std::string setup_check;
  if (!m.working_dir.empty()) {
    std::string setup_script = m.working_dir + "/install/setup.bash";
    setup_check = (access(setup_script.c_str(), F_OK) == 0) ? " (将自动 source)" : " (无 setup.bash)";
  }
  RCLCPP_INFO(this->get_logger(), "启动模块 %s | 工作目录: %s%s",
              name.c_str(), m.working_dir.empty() ? "(无)" : m.working_dir.c_str(), setup_check.c_str());

  int new_pid = 0;
  if (execCommand(cmd, m.working_dir, new_pid) < 0) return false;
  m.pid = new_pid; m.running = true; m.online = true;
  m.last_msg_time = this->now().seconds();  // 重置超时计数器
  RCLCPP_INFO(this->get_logger(), "模块 %s 已启动 (PID=%d): %s", name.c_str(), new_pid, cmd.c_str());
  return true;
}

bool ModuleManager::stopModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  auto &m = modules_[name];
  if (m.pid > 0) { killProcess(m.pid); m.pid = 0; }
  m.running = false; m.online = false;
  return true;
}

bool ModuleManager::restartModule(const std::string &name)
{
  stopModule(name);
  std::this_thread::sleep_for(1s);
  return startModule(name);
}

// =====================================================================
// 脚本任务执行（UDP task 触发，独立线程跑 .sh）
// =====================================================================
void ModuleManager::execScriptTask(const std::string &name)
{
  if (!script_tasks_.count(name)) { RCLCPP_WARN(this->get_logger(), "脚本任务 %s 不存在", name.c_str()); return; }
  auto &t = script_tasks_[name];
  if (!t.enabled) return;

  RCLCPP_INFO(this->get_logger(), "执行脚本任务 %s: %s", name.c_str(), t.script_path.c_str());
  std::thread([this, name]() {
    auto &t = script_tasks_[name];
    std::string cmd = "bash " + t.script_path;
    int ret = system(cmd.c_str());
    RCLCPP_INFO(this->get_logger(), "脚本任务 %s 结束, exit_code=%d", name.c_str(), ret);
  }).detach();
}

// =====================================================================
// 监控
// =====================================================================
void ModuleManager::monitorTimerCallback()
{
  double now = this->now().seconds();
  const double restart_interval = 3.0;
  static std::map<std::string, double> last_restart;

  for (auto &pair : modules_) {
    auto &m = pair.second;
    if (!m.enabled) continue;

    // 检查进程存活
    bool process_dead = (m.pid > 0 && !isProcessAlive(m.pid));
    if (process_dead) {
      RCLCPP_WARN(this->get_logger(), "模块 %s 进程已退出 (PID=%d)", pair.first.c_str(), m.pid);
      m.running = false; m.online = false;
    }

    // 超时判定：有 watch_topic 时才做超时检查
    bool timeout = !m.watch_topic.empty() && (now - m.last_msg_time > m.timeout_sec);
    if (timeout) {
      m.online = false;
    }

    // 无 watch_topic 的模块仅靠进程存活判定
    if (m.watch_topic.empty()) {
      m.online = m.running;
    }

    // 自动重启：进程死或超时，只要 auto_restart 就尝试重启
    if (m.auto_restart && (process_dead || timeout)) {
      auto it = last_restart.find(pair.first);
      if (it == last_restart.end() || now - it->second > restart_interval) {
        RCLCPP_WARN(this->get_logger(), "模块 %s 进程已退出/超时，自动重启", pair.first.c_str());
        restartModule(pair.first);
        last_restart[pair.first] = now;
      }
    }
  }
  publishModuleStatus();
}

void ModuleManager::publishModuleStatus()
{
  for (auto &pair : modules_) {
    auto &m = pair.second;
    module_manager_hub::msg::ModuleStatus msg;
    msg.name = m.name; msg.online = m.online; msg.running = m.running;
    msg.last_heartbeat = m.last_msg_time;
    msg.state_msg = m.online ? "RUNNING" : "LOST";
    status_pub_->publish(msg);
  }
}

// =====================================================================
// 服务回调
// =====================================================================
void ModuleManager::moduleControlCallback(
  const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
  std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res)
{
  res->success = false;
  if (!modules_.count(req->module_name)) { res->message = "模块不存在"; return; }
  switch (req->cmd) {
    case 1: res->success = startModule(req->module_name);  res->message = "已启动"; break;
    case 2: res->success = stopModule(req->module_name);   res->message = "已停止"; break;
    case 3: res->success = restartModule(req->module_name); res->message = "已重启"; break;
    case 4: modules_[req->module_name].enabled = false;    res->success = true;     res->message = "已禁用"; break;
    default: res->message = "指令无效";
  }
}