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

  // 从参数获取配置路径
  std::string config_path = "config/modules.yaml";
  this->declare_parameter<std::string>("config_path", config_path);
  this->get_parameter("config_path", config_path);

  loadConfig(config_path);

  monitor_timer_ = this->create_wall_timer(500ms, std::bind(&ModuleManager::monitorTimerCallback, this));
  RCLCPP_INFO(this->get_logger(), "人形机器人模块管理中枢 启动完成");
}

// 加载配置 + 订阅业务话题
void ModuleManager::loadConfig(const std::string &path)
{
  try {
    YAML::Node config = YAML::LoadFile(path);
    for (const auto &entry : config["modules"]) {
      Module m;
      m.name = entry.first.as<std::string>();
      m.node_name = entry.second["node_name"].as<std::string>();
      m.watch_topic = entry.second["watch_topic"].as<std::string>();
      m.enabled = entry.second["enabled"].as<bool>();
      m.auto_start = entry.second["auto_start"].as<bool>();
      m.auto_restart = entry.second["auto_restart"].as<bool>(); // 读取自动重启配置
      m.timeout_sec = entry.second["timeout_sec"].as<double>();
      m.last_msg_time = this->now().seconds();

      modules_[m.name] = m;

      // 通用订阅，监听业务话题
      auto cb = [this, name = m.name](const std::shared_ptr<rclcpp::SerializedMessage> msg) {
        this->topicCallback(msg, name);
      };
      auto sub = this->create_generic_subscription(
        m.watch_topic,
        "rosidl_typesupport_cpp",
        10,
        cb
      );
      topic_subs_[m.name] = sub;

      RCLCPP_INFO(this->get_logger(), "加载模块: %s | 监听话题: %s | 自动重启: %s",
                  m.name.c_str(), m.watch_topic.c_str(), m.auto_restart ? "true" : "false");

      // 自动启动
      if (m.enabled && m.auto_start) {
        startModule(m.name);
      }
    }
  } catch (std::exception &e) {
    RCLCPP_ERROR(this->get_logger(), "配置加载失败: %s", e.what());
  }
}

// 通用话题回调：更新最后消息时间
template <typename MsgT>
void ModuleManager::topicCallback(const std::shared_ptr<MsgT>, const std::string &mod_name)
{
  if (modules_.count(mod_name)) {
    modules_[mod_name].last_msg_time = this->now().seconds();
    modules_[mod_name].online = true;
    modules_[mod_name].running = true;
  }
}

// 定时巡检 + 自动重启核心逻辑
void ModuleManager::monitorTimerCallback()
{
  double now = this->now().seconds();
  // 重启防抖间隔：同一模块 3s 内不重复重启，防止死循环
  const double restart_interval = 3.0;
  static std::map<std::string, double> last_restart_time;

  for (auto &pair : modules_) {
    auto &m = pair.second;
    const std::string& mod_name = pair.first;
    if (!m.enabled) continue;

    double delta = now - m.last_msg_time;
    // 判定超时离线
    if (delta > m.timeout_sec) {
      m.online = false;
      m.running = false;

      // 开启自动重启 + 满足防抖间隔，执行重启
      if (m.auto_restart) {
        auto it = last_restart_time.find(mod_name);
        if (it == last_restart_time.end() || (now - it->second) > restart_interval) {
          RCLCPP_WARN(this->get_logger(), "模块[%s]超时离线，执行自动重启", mod_name.c_str());
          restartModule(mod_name);
          last_restart_time[mod_name] = now;
        }
      }
    }
  }
  publishModuleStatus();
}

// 启动模块
bool ModuleManager::startModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  auto &m = modules_[name];
  // 替换为你实际的功能包名
  std::string cmd = "ros2 run module_manager_hub " + m.node_name + " &";
  system(cmd.c_str());

  m.running = true;
  RCLCPP_INFO(this->get_logger(), "启动模块: %s", name.c_str());
  return true;
}

// 停止模块
bool ModuleManager::stopModule(const std::string &name)
{
  if (!modules_.count(name)) return false;
  std::string cmd = "pkill -f " + modules_[name].node_name;
  system(cmd.c_str());
  modules_[name].online = false;
  modules_[name].running = false;
  RCLCPP_INFO(this->get_logger(), "停止模块: %s", name.c_str());
  return true;
}

// 重启模块
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
  if (!modules_.count(req->module_name)) {
    res->message = "模块不存在";
    return;
  }

  switch (req->cmd) {
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

// 发布模块状态
void ModuleManager::publishModuleStatus()
{
  for (auto &pair : modules_) {
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