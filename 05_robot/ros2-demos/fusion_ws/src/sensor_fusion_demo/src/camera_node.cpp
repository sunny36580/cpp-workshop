#include "rclcpp/rclcpp.hpp"
#include "sensor_fusion_demo/msg/image_msg.hpp"

using namespace sensor_fusion_demo::msg;

class CameraNode : public rclcpp::Node
{
public:
    CameraNode() : Node("camera_node")
    {
        auto qos = rclcpp::QoS(10).best_effort();
        pub_ = this->create_publisher<ImageMsg>("camera_topic", qos);
        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(33),
            std::bind(&CameraNode::pub_cb, this)
        );
        RCLCPP_INFO(get_logger(), "Camera Node Start");
    }

private:
    void pub_cb()
    {
        ImageMsg msg;
        msg.stamp = this->now();
        msg.width = 640;
        msg.height = 480;
        pub_->publish(msg);
    }

    rclcpp::Publisher<ImageMsg>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char ** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CameraNode>());
    rclcpp::shutdown();
    return 0;
}