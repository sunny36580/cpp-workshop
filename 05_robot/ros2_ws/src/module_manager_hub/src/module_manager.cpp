#include "module_manager_hub/module_manager.h"
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <sys/wait.h>
#include <signal.h>
#include <algorithm>
#include <cstring>

using namespace std::chrono_literals;

// 全局信号处理
ModuleManager* g_module_manager = nullptr;
void signalHandler(int sig) {
  if (g_module_manager) {
    g_module_manager->handleShutdownSignal(sig);
  }
}

ModuleManager::ModuleManager(const std::string &name) : Node(name)
{
  g_module_manager = this;
  
  status_pub_ = this->create_publisher<module_manager_hub::msg::ModuleStatus>("/robot/modules_status", 10);
  ctrl_srv_ = this->create_service<module_manager_hub::srv::ModuleControl>(
    "/robot/module_control",
    std::bind(&ModuleManager::moduleControlCallback, this, std::placeholders::_1, std::placeholders::_2)
  );

  // 从参数中获取配置文件路径
  std::string config_path;
  this->declare_parameter<std::string>("config_path", "config/modules.yaml");
  config_path = this->get_parameter("config_path").as_string();

  RCLCPP_INFO(this->get_logger(), "加载配置文件: %s", config_path.c_str());
  loadConfig(config_path);

  // 注册信号处理
  signal(SIGINT, signalHandler);
  signal(SIGTERM, signalHandler);

  // 启动监控定时器（100ms精度）
  monitor_timer_ = this->create_wall_timer(100ms, std::bind(&ModuleManager::monitorTimerCallback, this));
  
  RCLCPP_INFO(this->get_logger(), "✅ 人形机器人模块管理中枢 启动就绪");
}

ModuleManager::~ModuleManager()
{
  gracefulShutdown();
}

void ModuleManager::loadConfig(const std::string &path)
{
  YAML::Node config = YAML::LoadFile(path);
  for (const auto &entry : config["modules"])
  {
    Module m;
    m.name = entry.first.as<std::string>();
    m.node_name = entry.second["node_name"].as<std::string>();
    m.monitor_topic = entry.second["monitor_topic"].as<std::string>();
    m.package_name = entry.second["package_name"].as<std::string>();
    m.enabled = entry.second["enabled"].as<bool>();
    m.auto_start = entry.second["auto_start"].as<bool>();
    m.heartbeat_timeout = entry.second["heartbeat_timeout"].as<double>();
    
    // 加载启动超时
    if (entry.second["startup_timeout"]) {
      m.startup_timeout = entry.second["startup_timeout"].as<double>();
    }
    
    // 加载重启配置
    if (entry.second["max_restart_count"]) {
      m.max_restart_count = entry.second["max_restart_count"].as<int>();
    }

    modules_[m.name] = m;

    RCLCPP_INFO(this->get_logger(), "加载模块配置: %s", m.name.c_str());

    // 自动创建心跳订阅
    auto heart_sub = this->create_subscription<std_msgs::msg::Empty>(
      m.monitor_topic, 10,
      [this, name=m.name](const std_msgs::msg::Empty::SharedPtr msg){
        this->heartbeatCallback(msg, name);
      }
    );
    heart_subs_[m.name] = heart_sub;

    // 自动创建就绪订阅
    std::string ready_topic = "/" + m.name + "/ready";
    auto ready_sub = this->create_subscription<std_msgs::msg::Empty>(
      ready_topic, 10,
      [this, name=m.name](const std_msgs::msg::Empty::SharedPtr msg){
        this->readyCallback(msg, name);
      }
    );
    ready_subs_[m.name] = ready_sub;

    // 自动启动（加入队列）
    if(m.enabled && m.auto_start)
    {
      startup_queue_.push(m.name);
    }
  }
}

// 就绪回调：模块初始化完成后发送
void ModuleManager::readyCallback(const std_msgs::msg::Empty::SharedPtr, const std::string &mod_name)
{
  if(!modules_.count(mod_name)) return;
  
  auto &m = modules_[mod_name];
  if (m.state == ModuleState::LOADING) {
    m.state = ModuleState::READY;
    m.restart_count = 0; // 启动成功，重置重启计数
    RCLCPP_INFO(this->get_logger(), "✅ 模块 %s 就绪", mod_name.c_str());
  }
}

// 心跳回调：运行中定期发送
void ModuleManager::heartbeatCallback(const std_msgs::msg::Empty::SharedPtr, const std::string &mod_name)
{
  if(!modules_.count(mod_name)) return;
  
  auto &m = modules_[mod_name];
  if (m.state == ModuleState::READY || m.state == ModuleState::RUNNING) {
    m.last_heartbeat = this->now().seconds();
    m.state = ModuleState::RUNNING;
  }
}

// 启动队列：直接并行启动所有模块
void ModuleManager::processStartupQueue()
{
  if (startup_queue_.empty()) {
    processing_startup_ = false;
    return;
  }
  
  processing_startup_ = true;
  
  // 一次性启动队列中所有模块（并行）
  while (!startup_queue_.empty()) {
    std::string module_name = startup_queue_.front();
    startup_queue_.pop();
    
    if (modules_.count(module_name)) {
      startModule(module_name);
    }
  }
  
  processing_startup_ = false;
}

// 启动模块
bool ModuleManager::startModule(const std::string &name)
{
  if (!modules_.count(name)) {
    RCLCPP_ERROR(this->get_logger(), "❌ 模块 %s 不存在", name.c_str());
    return false;
  }

  auto &m = modules_[name];
  
  // 检查状态
  if (m.state == ModuleState::LOADING || m.state == ModuleState::READY || m.state == ModuleState::RUNNING) {
    RCLCPP_WARN(this->get_logger(), "⚠️ 模块 %s 已在运行", name.c_str());
    return true;
  }
  
  // 检查是否达到最大重启次数
  if (m.restart_count >= m.max_restart_count) {
    RCLCPP_ERROR(this->get_logger(), "❌ 模块 %s 已达到最大重启次数(%d)，标记为永久故障", 
                 name.c_str(), m.max_restart_count);
    m.state = ModuleState::ERROR;
    m.error_message = "达到最大重启次数";
    return false;
  }

  RCLCPP_INFO(this->get_logger(), "🚀 启动模块: %s (第%d次尝试)", 
              name.c_str(), m.restart_count + 1);

  // 清理旧进程
  if (m.pid > 0) {
    kill(m.pid, SIGKILL);
    waitpid(m.pid, nullptr, 0);
    m.pid = -1;
  }

  pid_t pid = fork();
  if (pid == 0) {
    // 子进程执行命令（支持ros2 run、bash脚本、任何可执行文件）
    execlp("ros2", "ros2", "run",
           m.package_name.c_str(),
           m.node_name.c_str(),
           (char*)NULL);
    
    // 如果执行失败，尝试直接执行二进制/脚本
    std::string cmd = m.package_name + "/" + m.node_name;
    execlp(cmd.c_str(), cmd.c_str(), (char*)NULL);
    
    RCLCPP_FATAL(this->get_logger(), "❌ 执行命令失败: %s", strerror(errno));
    exit(1);
  }
  else if (pid > 0) {
    m.pid = pid;
    m.state = ModuleState::LOADING;
    m.startup_time = this->now().seconds();
    m.last_heartbeat = this->now().seconds();
    return true;
  }
  else {
    RCLCPP_ERROR(this->get_logger(), "❌ fork 进程失败: %s", strerror(errno));
    return false;
  }
}

// 停止模块（优雅关闭）
bool ModuleManager::stopModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  auto &m = modules_[name];

  if (m.state == ModuleState::UNLOADED || m.state == ModuleState::STOPPED) {
    return true;
  }

  RCLCPP_INFO(this->get_logger(), "⏹️ 停止模块: %s", name.c_str());

  if (m.pid > 0) {
    // 先发送SIGTERM，给2秒优雅关闭时间
    if (kill(m.pid, SIGTERM) == 0) {
      double start_time = this->now().seconds();
      while (this->now().seconds() - start_time < 2.0) {
        int status;
        if (waitpid(m.pid, &status, WNOHANG) == m.pid) {
          break;
        }
        std::this_thread::sleep_for(10ms);
      }
    }
    
    // 强制杀死
    if (kill(m.pid, SIGKILL) == 0) {
      waitpid(m.pid, nullptr, 0);
    }
  }

  m.pid = -1;
  m.state = ModuleState::STOPPED;
  m.error_message.clear();
  RCLCPP_INFO(this->get_logger(), "✅ 模块 %s 已停止", name.c_str());
  return true;
}

bool ModuleManager::restartModule(const std::string &name)
{
  stopModule(name);
  // 等待重启冷却时间
  if (modules_.count(name)) {
    auto &m = modules_[name];
    if ((size_t)m.restart_count < m.restart_intervals.size()) {
      double delay = m.restart_intervals[m.restart_count];
      if (delay > 0) {
        RCLCPP_INFO(this->get_logger(), "⏱️ 等待 %.1f 秒后重启模块 %s", delay, name.c_str());
        std::this_thread::sleep_for(std::chrono::duration<double>(delay));
      }
    }
    m.restart_count++;
  }
  return startModule(name);
}

// 处理模块故障
void ModuleManager::handleModuleFailure(const std::string &name, const std::string &reason)
{
  if (!modules_.count(name)) return;
  
  auto &m = modules_[name];
  RCLCPP_ERROR(this->get_logger(), "❌ 模块 %s 故障: %s", name.c_str(), reason.c_str());
  
  m.state = ModuleState::ERROR;
  m.error_message = reason;
  
  // 尝试重启
  if (m.restart_count < m.max_restart_count) {
    RCLCPP_INFO(this->get_logger(), "🔄 尝试重启模块 %s (%d/%d)", 
                name.c_str(), m.restart_count + 1, m.max_restart_count);
    restartModule(name);
  } else {
    RCLCPP_FATAL(this->get_logger(), "💀 模块 %s 永久故障，无法恢复", name.c_str());
  }
}

// 主监控循环（100ms一次）
void ModuleManager::monitorTimerCallback()
{
  double now_sec = this->now().seconds();
  
  // 处理启动队列
  if (!processing_startup_ && !startup_queue_.empty()) {
    processStartupQueue();
  }
  
  // 巡检所有模块
  for(auto &pair : modules_)
  {
    auto &m = pair.second;
    if(!m.enabled) continue;

    // 检查启动超时
    if (m.state == ModuleState::LOADING) {
      if (now_sec - m.startup_time > m.startup_timeout) {
        handleModuleFailure(m.name, "启动超时");
        continue;
      }
    }

    // 检查心跳超时
    if (m.state == ModuleState::RUNNING) {
      double diff = now_sec - m.last_heartbeat;
      if(diff > m.heartbeat_timeout) {
        handleModuleFailure(m.name, "心跳超时");
        continue;
      }
    }

    // 检查进程是否意外退出
    if (m.pid > 0) {
      int status;
      if (waitpid(m.pid, &status, WNOHANG) == m.pid) {
        std::string reason = "进程意外退出";
        if (WIFEXITED(status)) {
          reason += " (退出码: " + std::to_string(WEXITSTATUS(status)) + ")";
        } else if (WIFSIGNALED(status)) {
          reason += " (信号: " + std::to_string(WTERMSIG(status)) + ")";
        }
        handleModuleFailure(m.name, reason);
      }
    }
  }
  
  // 发布状态
  publishModuleStatus();
}

// 优雅关闭整个系统
void ModuleManager::gracefulShutdown()
{
  RCLCPP_INFO(this->get_logger(), "🛑 开始优雅关闭系统");
  
  // 按配置逆序停止（先加的后停）
  std::vector<std::string> stop_order;
  for (auto it = modules_.rbegin(); it != modules_.rend(); ++it) {
    stop_order.push_back(it->first);
  }
  
  // 按顺序停止
  for (const auto &name : stop_order) {
    stopModule(name);
  }
  
  RCLCPP_INFO(this->get_logger(), "✅ 系统优雅关闭完成");
}

void ModuleManager::handleShutdownSignal(int sig)
{
  RCLCPP_INFO(this->get_logger(), "收到信号 %d，开始关闭", sig);
  gracefulShutdown();
  rclcpp::shutdown();
}

void ModuleManager::publishModuleStatus()
{
  for(auto &pair : modules_)
  {
    auto &m = pair.second;
    module_manager_hub::msg::ModuleStatus msg;
    msg.name = m.name;
    msg.online = (m.state == ModuleState::READY || m.state == ModuleState::RUNNING);
    msg.running = (m.state == ModuleState::RUNNING);
    msg.last_heartbeat = m.last_heartbeat;
    
    // 状态消息
    switch (m.state) {
      case ModuleState::UNLOADED: msg.state_msg = "UNLOADED"; break;
      case ModuleState::LOADING: msg.state_msg = "LOADING"; break;
      case ModuleState::READY: msg.state_msg = "READY"; break;
      case ModuleState::RUNNING: msg.state_msg = "RUNNING"; break;
      case ModuleState::ERROR: msg.state_msg = "ERROR: " + m.error_message; break;
      case ModuleState::STOPPED: msg.state_msg = "STOPPED"; break;
    }
    
    status_pub_->publish(msg);
  }
}

void ModuleManager::moduleControlCallback(
  const std::shared_ptr<module_manager_hub::srv::ModuleControl::Request> req,
  std::shared_ptr<module_manager_hub::srv::ModuleControl::Response> res)
{
  res->success = false;
  if(!modules_.count(req->module_name))
  {
    res->message = "模块不存在";
    return;
  }
  
  auto &m = modules_[req->module_name];
  
  switch (req->cmd)
  {
    case 1: 
      if (m.state == ModuleState::ERROR && m.restart_count >= m.max_restart_count) {
        m.restart_count = 0; // 手动启动重置计数
      }
      startup_queue_.push(req->module_name);
      res->success = true; 
      res->message="已加入启动队列";
      break;
      
    case 2: 
      res->success = stopModule(req->module_name); 
      res->message="停止完成";
      break;
      
    case 3: 
      res->success = restartModule(req->module_name); 
      res->message="重启完成";
      break;
      
    case 4:
      m.enabled = false;
      stopModule(req->module_name);
      res->success = true;
      res->message = "模块已禁用";
      break;
      
    case 5:
      m.enabled = true;
      res->success = true;
      res->message = "模块已启用";
      break;
      
    default: 
      res->message="未知指令";
      break;
  }
}