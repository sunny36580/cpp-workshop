#include "rclcpp/rclcpp.hpp"
#include "sensor_fusion_demo/msg/world_model_msg.hpp"

using namespace sensor_fusion_demo::msg;

class WorldModelNode : public rclcpp::Node
{
public:
    WorldModelNode() : Node("world_model_node")
    {
        // 订阅最终融合结果
        sub_ = this->create_subscription<WorldModelMsg>(
            "fusion_result_topic", 10,
            std::bind(&WorldModelNode::callback, this, std::placeholders::_1)
        );

        RCLCPP_INFO(get_logger(), "World Model Node 启动完成，等待融合结果...");
    }

private:
    void callback(const WorldModelMsg::ConstSharedPtr msg)
    {
        RCLCPP_INFO(get_logger(), 
            "环境模型更新 | 障碍物数量: %d | 自车位置: [%.2f, %.2f, %.2f]",
            (int)(msg->obstacle_pos.size() / 3),
            msg->ego_pose[0], msg->ego_pose[1], msg->ego_pose[2]);
    }

    rclcpp::Subscription<WorldModelMsg>::SharedPtr sub_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<WorldModelNode>());
    rclcpp::shutdown();
    return 0;
}