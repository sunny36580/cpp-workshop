#include "module_manager_hub/module_manager.h"
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <cstdlib>
#include <ctime>

using namespace std::chrono_literals;

ModuleManager::ModuleManager(const std::string &name) : Node(name)
{
  status_pub_ = this->create_publisher<module_manager_hub::msg::ModuleStatus>("/robot/modules_status", 10);
  ctrl_srv_ = this->create_service<module_manager_hub::srv::ModuleControl>(
    "/robot/module_control",
    std::bind(&ModuleManager::moduleControlCallback, this, std::placeholders::_1, std::placeholders::_2)
  );

  // 从参数中获取配置文件路径，默认使用测试配置
  std::string config_path;
  this->declare_parameter<std::string>("config_path", "config/modules.yaml");
  config_path = this->get_parameter("config_path").as_string();

  RCLCPP_INFO(this->get_logger(), "加载配置文件: %s", config_path.c_str());
  loadConfig(config_path);

  // 启动监控定时器
  monitor_timer_ = this->create_wall_timer(500ms, std::bind(&ModuleManager::monitorTimerCallback, this));
  RCLCPP_INFO(this->get_logger(), "人形机器人模块管理中枢 启动就绪");
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
    m.enabled = entry.second["enabled"].as<bool>();
    m.auto_start = entry.second["auto_start"].as<bool>();
    m.heartbeat_timeout = entry.second["heartbeat_timeout"].as<double>();
    m.last_heartbeat = this->now().seconds();
    modules_[m.name] = m;

        // 自动创建心跳订阅
    auto sub = this->create_subscription<std_msgs::msg::Empty>(
      m.monitor_topic, 10,
      [this, name=m.name](const std_msgs::msg::Empty::SharedPtr msg){
        this->heartbeatCallback(msg, name);
      }
    );
    heart_subs_[m.name] = sub;

    // 自动启动
    if(m.enabled && m.auto_start)
    {
      startModule(m.name);
    }
  }
}

// 心跳刷新时间
void ModuleManager::heartbeatCallback(const std_msgs::msg::Empty::SharedPtr, const std::string &mod_name)
{
  if(modules_.count(mod_name))
  {
    modules_[mod_name].last_heartbeat = this->now().seconds();
    modules_[mod_name].online = true;
    modules_[mod_name].running = true;
  }
}

// 定时巡检：超时判定离线
void ModuleManager::monitorTimerCallback()
{
  double now_sec = this->now().seconds();
  for(auto &pair : modules_)
  {
    auto &m = pair.second;
    if(!m.enabled) continue;

    double diff = now_sec - m.last_heartbeat;
    if(diff > m.heartbeat_timeout)
    {
      m.online = false;
      m.running = false;
      // 可开启自动重启
      // restartModule(pair.first);
    }
  }
  publishModuleStatus();
}

// 启动ROS2节点
bool ModuleManager::startModule(const std::string &name)
{
  if(!modules_.count(name)) return false;
  auto &m = modules_[name];
  
  if(m.online && m.running) {
    RCLCPP_WARN(this->get_logger(), "模块 %s 已经在运行", name.c_str());
    return true;
  }
  
  m.online = true;
  m.running = true;
  m.last_heartbeat = this->now().seconds();
  
  RCLCPP_INFO(this->get_logger(), "启动模块: %s", name.c_str());
  return true;
}

// 停止节点
bool ModuleManager::stopModule(const std::string &name)
{
  if(!modules_.count(name)) return false;
  auto &m = modules_[name];
  
  if(!m.online && !m.running) {
    RCLCPP_WARN(this->get_logger(), "模块 %s 已经停止", name.c_str());
    return true;
  }
  
  m.online = false;
  m.running = false;
  
  RCLCPP_INFO(this->get_logger(), "停止模块: %s", name.c_str());
  return true;
}

bool ModuleManager::restartModule(const std::string &name)
{
  stopModule(name);
  rclcpp::sleep_for(1s);
  return startModule(name);
}

// 外部服务控制
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
  switch (req->cmd)
  {
    case 1: res->success = startModule(req->module_name); res->message="启动完成";break;
    case 2: res->success = stopModule(req->module_name); res->message="停止完成";break;
    case 3: res->success = restartModule(req->module_name); res->message="重启完成";break;
    default: res->message="未知指令";break;
  }
}

void ModuleManager::publishModuleStatus()
{
  for(auto &pair : modules_)
  {
    auto &m = pair.second;
    module_manager_hub::msg::ModuleStatus msg;
    msg.name = m.name;
    msg.online = m.online;
    msg.running = m.running;
    msg.last_heartbeat = m.last_heartbeat;
    msg.state_msg = m.online ? "NORMAL" : "ABNORMAL";
    status_pub_->publish(msg);
  }
}