#include "rclcpp/rclcpp.hpp"
#include "sensor_fusion_demo/msg/lidar_msg.hpp"

using namespace sensor_fusion_demo::msg;

class LidarNode : public rclcpp::Node
{
public:
    LidarNode() : Node("lidar_node")
    {
        auto qos = rclcpp::QoS(10).best_effort();
        pub_ = this->create_publisher<LidarMsg>("lidar_topic", qos);
        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(50),
            std::bind(&LidarNode::pub_cb, this)
        );
        RCLCPP_INFO(get_logger(), "Lidar Node Start");
    }

private:
    void pub_cb()
    {
        LidarMsg msg;
        msg.stamp = this->now();
        msg.point_num = 1024;
        pub_->publish(msg);
    }

    rclcpp::Publisher<LidarMsg>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<LidarNode>());
    rclcpp::shutdown();
    return 0;
}