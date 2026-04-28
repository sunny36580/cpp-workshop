#include "rclcpp/rclcpp.hpp"
#include "sensor_fusion_demo/msg/synced_bundle_msg.hpp"
#include "sensor_fusion_demo/msg/world_model_msg.hpp"

using namespace sensor_fusion_demo::msg;

class FusionNode : public rclcpp::Node
{
public:
    FusionNode() : Node("fusion_node")
    {
        // 订阅同步后的数据
        sub_ = this->create_subscription<SyncedBundleMsg>(
            "synced_bundle_topic", 10,
            std::bind(&FusionNode::callback, this, std::placeholders::_1)
        );

        // 发布融合结果
        pub_ = this->create_publisher<WorldModelMsg>("fusion_result_topic", 10);

        RCLCPP_INFO(get_logger(), "Fusion Node 启动完成，等待同步数据...");
    }

private:
    void callback(const SyncedBundleMsg::ConstSharedPtr msg)
    {
        RCLCPP_INFO(get_logger(), "开始执行多传感器融合");

        // 融合逻辑（你后续可以替换为自己的算法）
        WorldModelMsg result;
        result.stamp = this->now();
        
        // 模拟输出
        result.obstacle_pos = {1.0f, 2.0f, 3.0f};
        result.ego_pose = {0.0f, 0.0f, 0.0f};

        pub_->publish(result);
    }

    rclcpp::Subscription<SyncedBundleMsg>::SharedPtr sub_;
    rclcpp::Publisher<WorldModelMsg>::SharedPtr pub_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<FusionNode>());
    rclcpp::shutdown();
    return 0;
}