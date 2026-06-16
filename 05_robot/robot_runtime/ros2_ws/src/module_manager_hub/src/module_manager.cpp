#include "module_manager_hub/module_manager.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <thread>
#include <sstream>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <cstdio>

using namespace std::chrono_literals;

// =====================================================================
// 构造函数 & 析构
// =====================================================================
ModuleManager::ModuleManager(const std::string &name)
  : Node(name), serial_port_(io_context_), current_mode_("STAND"), hw_switch_state_(false)
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

  // 串口配置
  if (root_cfg["serial"]) {
    auto s = root_cfg["serial"];
    if (s["port"])      serial_cfg_.port      = s["port"].as<std::string>();
    if (s["baud_rate"]) serial_cfg_.baud_rate = s["baud_rate"].as<int>();
  }
  RCLCPP_INFO(this->get_logger(), "串口配置: %s @ %d baud", serial_cfg_.port.c_str(), serial_cfg_.baud_rate);

  if (root_cfg["modules"])      loadConfig(root_cfg["modules"]);
  if (root_cfg["cmd_route"])    loadCmdRoute(root_cfg["cmd_route"]);
  if (root_cfg["script_tasks"]) loadScriptTasks(root_cfg["script_tasks"]);

  monitor_timer_ = this->create_wall_timer(500ms, std::bind(&ModuleManager::monitorTimerCallback, this));

  // 遥控话题
  cmd_vel_pub_    = this->create_publisher<geometry_msgs::msg::Twist>("/cmd_vel", 10);
  control_mode_pub_ = this->create_publisher<std_msgs::msg::String>("/control_mode", 10);
  hw_switch_pub_  = this->create_publisher<std_msgs::msg::Bool>("/hwswitch", 10);
  action_cmd_pub_ = this->create_publisher<std_msgs::msg::String>("/action_cmd", 10);

  // 50Hz 速度插值线程（独立线程 + Rate 精确控制）
  speed_running_ = true;
  speed_thread_ = std::thread(&ModuleManager::interpolateSpeedLoop, this);

  initSerial();
  io_thread_ = std::thread([this]() { io_context_.run(); });


  RCLCPP_INFO(this->get_logger(), "人形机器人中枢启动完成 ✅");
}

ModuleManager::~ModuleManager()
{
  speed_running_ = false;
  if (speed_thread_.joinable()) speed_thread_.join();

  // 先关串口，触发所有挂起的 async_read 以 operation_aborted 返回
  if (serial_port_.is_open()) {
    boost::system::error_code ec;
    serial_port_.close(ec);
  }
  io_context_.stop();
  if (io_thread_.joinable()) io_thread_.join();

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
    t.setup_path  = entry.second["setup_path"] ? entry.second["setup_path"].as<std::string>() : "";
    t.enabled     = entry.second["enabled"] ? entry.second["enabled"].as<bool>() : true;
    script_tasks_[t.name] = t;
    std::string setup_info = t.setup_path.empty() ? "" : (" setup=" + t.setup_path);
    RCLCPP_INFO(this->get_logger(), "脚本任务注册: %s → %s%s", t.name.c_str(), t.script_path.c_str(), setup_info.c_str());
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
// 串口通信（替代 UDP）
// =====================================================================
void ModuleManager::initSerial()
{
  try {
    serial_port_.open(serial_cfg_.port);
    serial_port_.set_option(boost::asio::serial_port::baud_rate(serial_cfg_.baud_rate));
    serial_port_.set_option(boost::asio::serial_port::flow_control(boost::asio::serial_port::flow_control::none));
    serial_port_.set_option(boost::asio::serial_port::parity(boost::asio::serial_port::parity::none));
    serial_port_.set_option(boost::asio::serial_port::stop_bits(boost::asio::serial_port::stop_bits::one));
    serial_port_.set_option(boost::asio::serial_port::character_size(8));
    doSerialRead();
    RCLCPP_INFO(this->get_logger(), "串口已打开: %s @ %d baud", serial_cfg_.port.c_str(), serial_cfg_.baud_rate);
  } catch (std::exception &e) {
    RCLCPP_ERROR(this->get_logger(), "串口打开失败 %s: %s", serial_cfg_.port.c_str(), e.what());
  }
}

void ModuleManager::doSerialRead()
{
  serial_port_.async_read_some(buffer(serial_rx_buf_),
    [this](std::error_code ec, std::size_t bytes) {
      if (ec) {
        serial_rx_frame_.clear();
        if (rclcpp::ok()) {
          RCLCPP_WARN(this->get_logger(), "串口读错误，恢复中: %s", ec.message().c_str());
          doSerialRead();
        }
        return;
      }

      // 将新数据追加到帧缓存
      serial_rx_frame_.insert(serial_rx_frame_.end(),
                              serial_rx_buf_.begin(), serial_rx_buf_.begin() + bytes);

      // 尝试从缓存中解析完整帧（32 字节固定帧长）
      while (true) {
        auto &buf = serial_rx_frame_;
        if (buf.size() < SERIAL_FRAME_LEN) break;

        // 找帧头 0xAA 0x55
        auto sof_it = std::search(buf.begin(), buf.end(),
                                  serial_rx_frame_end_marker_, serial_rx_frame_end_marker_ + 2);
        if (sof_it == buf.end()) {
          buf.clear();
          break;
        }

        // 扔掉帧头之前的数据
        if (sof_it != buf.begin()) {
          buf.erase(buf.begin(), sof_it);
        }

        if (buf.size() < SERIAL_FRAME_LEN) break;

        // CRC16 校验（从 CmdType 到保留末尾，共 30 字节）
        uint16_t recv_crc = static_cast<uint16_t>(buf[SERIAL_FRAME_LEN - 2]) |
                            (static_cast<uint16_t>(buf[SERIAL_FRAME_LEN - 1]) << 8);
        uint16_t calc_crc = calcCRC16(buf.data() + 2, SERIAL_FRAME_LEN - 4);

        if (recv_crc != calc_crc) {
          RCLCPP_WARN(this->get_logger(), "串口帧 CRC16 校验错误, 丢弃");
          buf.erase(buf.begin(), buf.begin() + SERIAL_FRAME_LEN);
          continue;
        }

        // 解析并执行
        uint8_t cmd_type = buf[2];
        uint8_t pay_len  = buf[3];
        parseSerialPacket(buf.data() + SERIAL_HEADER_LEN, std::min(pay_len, static_cast<uint8_t>(SERIAL_DATA_LEN)), cmd_type);

        // 移除已处理的帧
        buf.erase(buf.begin(), buf.begin() + SERIAL_FRAME_LEN);
      }

      doSerialRead();
    });
}

uint16_t ModuleManager::calcCRC16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

void ModuleManager::buildSerialFrame(uint8_t cmd_type, const uint8_t *payload, size_t payload_len,
                                     std::vector<uint8_t> &out_frame)
{
  out_frame.clear();
  out_frame.reserve(SERIAL_FRAME_LEN);

  // 帧头 2 字节
  out_frame.push_back(SERIAL_SOF0);
  out_frame.push_back(SERIAL_SOF1);

  // 指令类型 + 数据长度
  out_frame.push_back(cmd_type);
  out_frame.push_back(static_cast<uint8_t>(std::min(payload_len, SERIAL_DATA_LEN)));

  // 数据域 16 字节（填充 0）
  uint8_t data_field[SERIAL_DATA_LEN] = {0};
  size_t copy_len = std::min(payload_len, SERIAL_DATA_LEN);
  std::memcpy(data_field, payload, copy_len);
  out_frame.insert(out_frame.end(), data_field, data_field + SERIAL_DATA_LEN);

  // 保留 10 字节（全 0）
  uint8_t reserved[SERIAL_RESERVED] = {0};
  out_frame.insert(out_frame.end(), reserved, reserved + SERIAL_RESERVED);

  // CRC16（从 CmdType 到保留末尾）
  uint16_t crc = calcCRC16(out_frame.data() + 2, SERIAL_FRAME_LEN - 4);
  out_frame.push_back(static_cast<uint8_t>(crc & 0xFF));
  out_frame.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

void ModuleManager::sendSerialResponse(uint8_t cmd_type, const uint8_t *payload, size_t payload_len)
{
  auto frame = std::make_shared<std::vector<uint8_t>>();
  buildSerialFrame(cmd_type, payload, payload_len, *frame);

  boost::asio::async_write(serial_port_, boost::asio::buffer(*frame),
    [this, frame](boost::system::error_code ec, std::size_t /*bytes*/) {
      if (ec) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
          "串口异步写失败: %s", ec.message().c_str());
      }
    });
}

void ModuleManager::parseSerialPacket(const uint8_t *payload, size_t pay_len, uint8_t cmd_type)
{
  // RCLCPP_INFO(this->get_logger(), "收到串口指令: type=%d, len=%zu", cmd_type, pay_len);

  auto publish_mode = [this]() {
    std_msgs::msg::String m; m.data = current_mode_; control_mode_pub_->publish(m);
  };

  switch (cmd_type)
  {
    // ---- CMD_MOVE: 运动指令（仅更新目标速度，由 speed_timer_ 以 50Hz 插值输出） ----
    case CMD_MOVE: {
      if (pay_len < 8) { RCLCPP_WARN(this->get_logger(), "运动指令长度不足"); break; }
      float linear  = 0, angular = 0;
      memcpy(&linear,  payload,      sizeof(float));
      memcpy(&angular, payload + 4,  sizeof(float));

      // 限幅
      target_linear_  = std::clamp(linear,  -static_cast<float>(LINEAR_MAX),   static_cast<float>(LINEAR_MAX));
      target_angular_ = std::clamp(angular, -static_cast<float>(ANGULAR_MAX), static_cast<float>(ANGULAR_MAX));

      if ((std::abs(target_linear_) > 0.001f || std::abs(target_angular_) > 0.001f) && current_mode_ == "STAND") {
        current_mode_ = "WALK_FULL";
        publish_mode();
      }
      RCLCPP_INFO(this->get_logger(), "运动指令: linear=%.3f, angular=%.3f", target_linear_.load(), target_angular_.load());
      break;
    }

    // ---- CMD_TASK: 任务指令（数字键） ----
    case CMD_TASK: {
      // 防误触：2秒内连续的数字键命令只响应第一个
      double now = this->now().seconds();
      if (now - last_task_cmd_time_ < TASK_CMD_DEBOUNCE_SEC) {
        RCLCPP_WARN(this->get_logger(), "防误触: 忽略连续任务指令 (距上次 %.2fs < %.1fs)",
                    now - last_task_cmd_time_, TASK_CMD_DEBOUNCE_SEC);
        break;
      }
      last_task_cmd_time_ = now;

      if (pay_len < 1) { RCLCPP_WARN(this->get_logger(), "任务指令长度不足"); break; }
      uint8_t task_id = payload[0];

      // 发 action_cmd 话题
      static const char* actions[] = {"WAVE","HANDSHAKE","PERFORM","TURN_180","FINGER_SHOW","WAVE","SMILE","STAND"};
      if (task_id >= 1 && task_id <= 8) {
        std_msgs::msg::String a;
        a.data = actions[task_id - 1];
        action_cmd_pub_->publish(a);
        RCLCPP_INFO(this->get_logger(), "转发 action_cmd: %s", a.data.c_str());
      }

      // 执行同名脚本任务
      std::string task_name = "task_" + std::to_string(task_id);
      if (script_tasks_.count(task_name) && script_tasks_[task_name].enabled) {
        execScriptTask(task_name);
      }
      break;
    }

    // ---- CMD_HEARTBEAT / CMD_STATUS: 控制端复用 0x03 作急停 ----
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

    // 子进程创建独立进程组以便统一杀掉整个进程树
    setpgid(0, 0);
    // 注意: 不调 setsid()，否则脱离父进程会话后 kill(-pgid) 无法送达
    execl("/bin/sh", "sh", "-c", full_cmd.c_str(), (char*)nullptr);
    _exit(127);
  }
  out_pid = pid;
  RCLCPP_INFO(this->get_logger(), "启动子进程 PID=%d: %s", pid, cmd.c_str());
  return pid;
}

// 递归杀掉指定 PID 及其所有子进程（遍历 /proc）
static void killProcessTree(int pid, int sig)
{
  if (pid <= 0) return;
  // 先收集子进程列表
  std::vector<int> children;
  DIR *proc = opendir("/proc");
  if (proc) {
    struct dirent *entry;
    while ((entry = readdir(proc)) != nullptr) {
      if (entry->d_type != DT_DIR) continue;
      char status_path[64];
      snprintf(status_path, sizeof(status_path), "/proc/%s/status", entry->d_name);
      FILE *f = fopen(status_path, "r");
      if (!f) continue;
      char line[256];
      int ppid = -1;
      while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "PPid:\t%d", &ppid) == 1) break;
      }
      fclose(f);
      if (ppid == pid) {
        int child_pid = atoi(entry->d_name);
        children.push_back(child_pid);
      }
    }
    closedir(proc);
  }
  // 先递归杀子进程，再杀自己
  for (int child : children) {
    killProcessTree(child, sig);
  }
  kill(pid, sig);
}

bool ModuleManager::killProcess(int pid)
{
  if (pid <= 0) return false;

  // 递归杀掉整个进程树（sh -c → python3 → camera_streamer_node）
  killProcessTree(pid, SIGTERM);

  for (int i = 0; i < 10; i++) {
    if (waitpid(pid, nullptr, WNOHANG) > 0) return true;
    std::this_thread::sleep_for(500ms);
  }

  // 5 秒后强制 SIGKILL
  killProcessTree(pid, SIGKILL);
  waitpid(pid, nullptr, 0);
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
    std::string cmd;
    if (!t.setup_path.empty()) {
      // 用 . 代替 source（POSIX 兼容），bash -c 确保环境变量生效
      cmd = "bash -c '. " + t.setup_path + " && " + t.script_path + "'";
    } else {
      cmd = "bash " + t.script_path;
    }
    int ret = system(cmd.c_str());
    RCLCPP_INFO(this->get_logger(), "脚本任务 %s 结束, exit_code=%d", name.c_str(), ret);
  }).detach();
}

// =====================================================================
// 监控
// =====================================================================
// 速度插值独立线程：50Hz 以恒定加减速逼近目标速度
// =====================================================================
void ModuleManager::interpolateSpeedLoop()
{
  rclcpp::Rate rate(SPEED_INTERPOLATE_HZ);
  constexpr double dt = 1.0 / SPEED_INTERPOLATE_HZ;  // 动态计算步长

  while (rclcpp::ok() && speed_running_) {

    // 原子变量快照（避免跨线程反复读取 atomic 导致性能问题）
    float tgt_linear  = target_linear_.load(std::memory_order_relaxed);
    float tgt_angular = target_angular_.load(std::memory_order_relaxed);

    // ---- 线速度插值 ----
    if (std::abs(tgt_linear - current_linear_) < 0.001f) {
      current_linear_ = tgt_linear;
    } else {
      float step = LINEAR_ACCEL * dt;
      if (tgt_linear > current_linear_) {
        current_linear_ = std::min(current_linear_ + step, tgt_linear);
      } else {
        current_linear_ = std::max(current_linear_ - step, tgt_linear);
      }
    }

    // ---- 角速度插值 ----
    if (std::abs(tgt_angular - current_angular_) < 0.001f) {
      current_angular_ = tgt_angular;
    } else {
      float step = ANGULAR_ACCEL * dt;
      if (tgt_angular > current_angular_) {
        current_angular_ = std::min(current_angular_ + step, tgt_angular);
      } else {
        current_angular_ = std::max(current_angular_ - step, tgt_angular);
      }
    }

    // ---- 发布 cmd_vel ----
    // 只有速度不为零时才发布；目标与当前均为零时跳过发布（运控保持上次指令）
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
  sendSerialStatus();
}

void ModuleManager::sendSerialStatus()
{
  // 直接在 ROS 线程同步写，32 字节帧在 115200 波特率下耗时 < 3ms
  static const char* mod_ids[] = {
    "lower_body", "upper_body", "imu_driver",
    "remote_interface", "usb_camera", nullptr
  };
  uint16_t mask = 0;
  for (int i = 0; mod_ids[i] != nullptr; i++) {
    if (modules_.count(mod_ids[i]) && modules_[mod_ids[i]].online)
      mask |= (1 << i);
  }

  // 状态反馈 payload: 模块状态位图(2B) + 控制模式(1B) + 开关状态(1B) + 保留(12B)
  uint8_t payload[SERIAL_DATA_LEN] = {0};
  payload[0] = static_cast<uint8_t>((mask >> 8) & 0xFF);
  payload[1] = static_cast<uint8_t>(mask & 0xFF);
  // payload[2] 可用于扩展

  // 拼帧并同步发送
  std::vector<uint8_t> frame;
  buildSerialFrame(CMD_STATUS, payload, SERIAL_DATA_LEN, frame);

  boost::system::error_code ec;
  boost::asio::write(serial_port_, boost::asio::buffer(frame), ec);
  if (ec) {
    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
      "串口状态发送失败: %s", ec.message().c_str());
  }
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