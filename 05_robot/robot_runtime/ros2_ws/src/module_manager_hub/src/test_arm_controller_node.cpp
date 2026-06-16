#include <rclcpp/rclcpp.hpp>
#include <module_manager_hub/msg/robotarmcontrol.hpp>
#include <module_manager_hub/msg/robotarmstatus.hpp>
#include <chrono>
#include <random>
#include <vector>

using namespace std::chrono_literals;

class TestArmController : public rclcpp::Node {
public:
    explicit TestArmController(const std::string& node_name)
    : Node(node_name), motor_count_(6) {
        // 初始化电机状态
        motor_ids_.resize(motor_count_);
        actual_positions_.resize(motor_count_, 0.0f);
        actual_velocities_.resize(motor_count_, 0.0f);
        
        for (uint32_t i = 0; i < motor_count_; i++) {
            motor_ids_[i] = i + 1; // 电机ID从1开始
        }

        // 创建状态发布者（用于模块管理器的心跳监控）
        status_pub_ = this->create_publisher<module_manager_hub::msg::Robotarmstatus>(
            "/arm/status", 10);

        // 创建控制指令订阅者（接收来自模块管理器的指令）
        control_sub_ = this->create_subscription<module_manager_hub::msg::Robotarmcontrol>(
            "/arm/control", 10,
            std::bind(&TestArmController::controlCallback, this, std::placeholders::_1));

        // 创建状态发布定时器（10Hz，确保心跳足够频繁）
        status_timer_ = this->create_wall_timer(
            100ms, std::bind(&TestArmController::publishStatus, this));

        RCLCPP_INFO(this->get_logger(), "✅ 测试机械臂控制器启动成功");
        RCLCPP_INFO(this->get_logger(), "   - 电机数量: %d", motor_count_);
        RCLCPP_INFO(this->get_logger(), "   - 状态发布频率: 10Hz");
        RCLCPP_INFO(this->get_logger(), "   - 控制话题: /arm/control");
        RCLCPP_INFO(this->get_logger(), "   - 状态话题: /arm/status");
    }

    ~TestArmController() override {
        RCLCPP_INFO(this->get_logger(), "❌ 测试机械臂控制器已停止");
    }

private:
    void controlCallback(const module_manager_hub::msg::Robotarmcontrol::SharedPtr msg) {
        RCLCPP_INFO(this->get_logger(), "📥 收到控制指令");
        RCLCPP_INFO(this->get_logger(), "   - 电机数量: %zu", msg->motor_ids.size());
        
        // 检查指令有效性
        if (msg->motor_ids.size() != msg->target_positions.size() ||
            msg->motor_ids.size() != msg->target_velocitie.size()) {
            RCLCPP_ERROR(this->get_logger(), "   ❌ 指令格式错误: 数组长度不匹配");
            return;
        }

        // 更新电机目标状态
        for (size_t i = 0; i < msg->motor_ids.size(); i++) {
            uint32_t motor_id = msg->motor_ids[i];
            if (motor_id >= 1 && motor_id <= motor_count_) {
                size_t index = motor_id - 1;
                actual_positions_[index] = msg->target_positions[i];
                actual_velocities_[index] = msg->target_velocitie[i];
                
                RCLCPP_DEBUG(this->get_logger(), 
                    "   - 电机%d: 位置=%.2f, 速度=%.2f",
                    motor_id, 
                    msg->target_positions[i], 
                    msg->target_velocitie[i]);
            } else {
                RCLCPP_WARN(this->get_logger(), "   ⚠️  无效电机ID: %d", motor_id);
            }
        }
        
        RCLCPP_INFO(this->get_logger(), "   ✅ 指令执行完成");
    }

    void publishStatus() {
        auto msg = module_manager_hub::msg::Robotarmstatus();
        msg.motor_ids = motor_ids_;
        msg.actual_positions = actual_positions_;
        msg.actual_velocities = actual_velocities_;
        
        // 添加微小噪声模拟真实传感器
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::normal_distribution<float> noise(0.0f, 0.01f);
        
        for (auto& pos : msg.actual_positions) {
            pos += noise(gen);
        }
        
        status_pub_->publish(msg);
        
        // 每5秒打印一次状态摘要
        static int counter = 0;
        if (++counter >= 50) {
            RCLCPP_INFO(this->get_logger(), "📤 发布状态 - 电机1位置: %.2f", actual_positions_[0]);
            counter = 0;
        }
    }

    const uint32_t motor_count_;
    std::vector<uint32_t> motor_ids_;
    std::vector<float> actual_positions_;
    std::vector<float> actual_velocities_;

    rclcpp::Publisher<module_manager_hub::msg::Robotarmstatus>::SharedPtr status_pub_;
    rclcpp::Subscription<module_manager_hub::msg::Robotarmcontrol>::SharedPtr control_sub_;
    rclcpp::TimerBase::SharedPtr status_timer_;
};

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<TestArmController>("test_arm_controller_node");
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}