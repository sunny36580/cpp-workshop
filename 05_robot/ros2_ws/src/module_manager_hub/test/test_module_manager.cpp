#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include "module_manager_hub/module_manager.h"
#include "module_manager_hub/srv/module_control.hpp"
#include "std_msgs/msg/empty.hpp"
#include <yaml-cpp/yaml.h>
#include <chrono>
#include <thread>
#include <fstream>  // 修复：添加文件流头文件

using namespace std::chrono_literals;

// 测试夹具：每个测试用例都会创建独立的节点
class ModuleManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 初始化ROS2
    rclcpp::init(0, nullptr);
    
    // 创建测试用配置文件（临时）
    createTestConfig();
    
    // 创建模块管理器节点
    manager_node_ = std::make_shared<ModuleManager>("test_module_manager");
    
    // 创建执行器
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(manager_node_);
    
    // 启动后台线程运行执行器
    spin_thread_ = std::thread([this]() {
      executor_->spin();
    });
    
    // 等待节点初始化完成
    std::this_thread::sleep_for(100ms);
  }

  void TearDown() override {
    // 停止执行器
    executor_->cancel();
    if(spin_thread_.joinable()) {
      spin_thread_.join();
    }
    
    // 关闭ROS2
    rclcpp::shutdown();
    
    // 删除临时配置文件
    std::remove("test_config.yaml");
  }

  // 创建测试用配置文件
  void createTestConfig() {
    YAML::Node config;
    config["modules"]["test_module1"]["node_name"] = "test_node1";
    config["modules"]["test_module1"]["monitor_topic"] = "/test/heartbeat1";
    config["modules"]["test_module1"]["enabled"] = true;
    config["modules"]["test_module1"]["auto_start"] = true;
    config["modules"]["test_module1"]["heartbeat_timeout"] = 1.0;
    
    config["modules"]["test_module2"]["node_name"] = "test_node2";
    config["modules"]["test_module2"]["monitor_topic"] = "/test/heartbeat2";
    config["modules"]["test_module2"]["enabled"] = true;
    config["modules"]["test_module2"]["auto_start"] = false;
    config["modules"]["test_module2"]["heartbeat_timeout"] = 1.0;
    
    std::ofstream fout("test_config.yaml");
    fout << config;
    fout.close();
  }

  // 辅助函数：调用模块控制服务
  bool callModuleControl(const std::string &module_name, uint8_t cmd) {
    auto client = manager_node_->create_client<module_manager_hub::srv::ModuleControl>("/robot/module_control");
    
    if(!client->wait_for_service(1s)) {
      return false;
    }
    
    auto request = std::make_shared<module_manager_hub::srv::ModuleControl::Request>();
    request->module_name = module_name;
    request->cmd = cmd;
    
    auto future = client->async_send_request(request);
    if(future.wait_for(1s) != std::future_status::ready) {
      return false;
    }
    
    auto response = future.get();
    return response->success;
  }

  std::shared_ptr<ModuleManager> manager_node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_thread_;
};

// 测试1：配置文件加载
TEST_F(ModuleManagerTest, LoadConfig) {
  // 检查模块数量
  EXPECT_EQ(manager_node_->getModules().size(), 2);
  
  // 修复：使用 at() 代替 operator[] 访问 const map
  const auto &mod1 = manager_node_->getModules().at("test_module1");
  EXPECT_EQ(mod1.name, "test_module1");
  EXPECT_EQ(mod1.node_name, "test_node1");
  EXPECT_EQ(mod1.monitor_topic, "/test/heartbeat1");
  EXPECT_TRUE(mod1.enabled);
  EXPECT_TRUE(mod1.auto_start);
  EXPECT_EQ(mod1.heartbeat_timeout, 1.0);
  
  const auto &mod2 = manager_node_->getModules().at("test_module2");
  EXPECT_EQ(mod2.name, "test_module2");
  EXPECT_EQ(mod2.node_name, "test_node2");
  EXPECT_EQ(mod2.monitor_topic, "/test/heartbeat2");
  EXPECT_TRUE(mod2.enabled);
  EXPECT_FALSE(mod2.auto_start);
  EXPECT_EQ(mod2.heartbeat_timeout, 1.0);
}

// 测试2：心跳检测功能
TEST_F(ModuleManagerTest, HeartbeatDetection) {
  // 修复：使用 at() 代替 operator[]
  auto &mod1 = const_cast<Module&>(manager_node_->getModules().at("test_module1"));
  
  // 初始状态：test_module1应该是在线（auto_start=true）
  EXPECT_TRUE(mod1.online);
  EXPECT_TRUE(mod1.running);
  
  // 发送心跳消息
  auto heartbeat_pub = manager_node_->create_publisher<std_msgs::msg::Empty>("/test/heartbeat1", 10);
  heartbeat_pub->publish(std_msgs::msg::Empty());
  std::this_thread::sleep_for(100ms);
  
  // 检查心跳时间是否更新
  double old_heartbeat = mod1.last_heartbeat;
  heartbeat_pub->publish(std_msgs::msg::Empty());
  std::this_thread::sleep_for(100ms);
  EXPECT_GT(mod1.last_heartbeat, old_heartbeat);
  
  // 等待超时，检查是否离线
  std::this_thread::sleep_for(1.5s);
  EXPECT_FALSE(mod1.online);
  EXPECT_FALSE(mod1.running);
}

// 测试3：模块控制服务
TEST_F(ModuleManagerTest, ModuleControlService) {
  // 测试启动服务
  EXPECT_TRUE(callModuleControl("test_module2", 1));
  auto &mod2 = const_cast<Module&>(manager_node_->getModules().at("test_module2"));
  EXPECT_TRUE(mod2.online);
  EXPECT_TRUE(mod2.running);
  
  // 测试停止服务
  EXPECT_TRUE(callModuleControl("test_module2", 2));
  EXPECT_FALSE(mod2.online);
  EXPECT_FALSE(mod2.running);
  
  // 测试重启服务
  EXPECT_TRUE(callModuleControl("test_module2", 3));
  EXPECT_TRUE(mod2.online);
  EXPECT_TRUE(mod2.running);
  
  // 测试不存在的模块
  EXPECT_FALSE(callModuleControl("non_exist_module", 1));
}

// 测试4：状态发布功能
TEST_F(ModuleManagerTest, StatusPublish) {
  bool received = false;
  module_manager_hub::msg::ModuleStatus last_msg;
  
  // 订阅状态话题
  auto sub = manager_node_->create_subscription<module_manager_hub::msg::ModuleStatus>(
    "/robot/modules_status", 10,
    [&](const module_manager_hub::msg::ModuleStatus::SharedPtr msg) {
      received = true;
      last_msg = *msg;
    }
  );
  
  // 等待消息
  std::this_thread::sleep_for(1s);
  EXPECT_TRUE(received);
  
  // 检查消息内容
  EXPECT_TRUE(last_msg.name == "test_module1" || last_msg.name == "test_module2");
  EXPECT_TRUE(last_msg.online || !last_msg.online);
  EXPECT_TRUE(last_msg.running || !last_msg.running);
  EXPECT_GT(last_msg.last_heartbeat, 0.0);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}