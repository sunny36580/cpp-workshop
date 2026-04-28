#include "rclcpp/rclcpp.hpp"
#include "message_filters/subscriber.h"
#include "message_filters/sync_policies/approximate_time.h"
#include "message_filters/synchronizer.h"

#include "sensor_fusion_demo/msg/image_msg.hpp"
#include "sensor_fusion_demo/msg/lidar_msg.hpp"
#include "sensor_fusion_demo/msg/synced_bundle_msg.hpp"

using namespace sensor_fusion_demo::msg;
namespace mf = message_filters;

class SyncNode : public rclcpp::Node
{
public:
    SyncNode() : Node("sync_node")
    {
        // ====================== 修复 QoS 兼容问题 ======================
        // 传感器话题统一使用 BEST_EFFORT
        auto qos_profile = rclcpp::QoS(10).best_effort();

        // message_filters 订阅 + 正确绑定 QoS
        cam_sub_.subscribe(this, "camera_topic", qos_profile.get_rmw_qos_profile());
        lidar_sub_.subscribe(this, "lidar_topic", qos_profile.get_rmw_qos_profile());

        // ====================== 修复同步器初始化 ======================
        // 时间同步策略（近似时间同步）
        using SyncPolicy = mf::sync_policies::ApproximateTime<ImageMsg, LidarMsg>;
        sync_ = std::make_shared<mf::Synchronizer<SyncPolicy>>(SyncPolicy(10), cam_sub_, lidar_sub_);
        sync_->registerCallback(std::bind(&SyncNode::sync_callback, this, std::placeholders::_1, std::placeholders::_2));

        // 发布同步后的数据
        sync_pub_ = this->create_publisher<SyncedBundleMsg>("synced_bundle_topic", 10);

        RCLCPP_INFO(get_logger(), "✅ Sync Node 启动成功");
    }

private:
    // 同步成功回调
    void sync_callback(const ImageMsg::ConstSharedPtr cam_msg, const LidarMsg::ConstSharedPtr lidar_msg)
    {
        RCLCPP_INFO(get_logger(), "🔄 相机 + 雷达数据同步成功");

        // 封装数据包并发布
        SyncedBundleMsg bundle_msg;
        bundle_msg.stamp = this->now();
        bundle_msg.image = *cam_msg;
        bundle_msg.lidar = *lidar_msg;
        sync_pub_->publish(bundle_msg);
    }

    // 订阅器 & 同步器
    mf::Subscriber<ImageMsg> cam_sub_;
    mf::Subscriber<LidarMsg> lidar_sub_;
    std::shared_ptr<mf::Synchronizer<mf::sync_policies::ApproximateTime<ImageMsg, LidarMsg>>> sync_;
    
    // 发布器
    rclcpp::Publisher<SyncedBundleMsg>::SharedPtr sync_pub_;
};

int main(int argc, char * argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SyncNode>());
    rclcpp::shutdown();
    return 0;
}