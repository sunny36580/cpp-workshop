#pragma once
#include <chrono>
#include <optional>
#include <sensor_fusion_demo/msg/image_msg.hpp>
#include <sensor_fusion_demo/msg/lidar_msg.hpp>

namespace sensor_fusion_demo
{

using ImageMsg = sensor_fusion_demo::msg::ImageMsg;
using LidarMsg = sensor_fusion_demo::msg::LidarMsg;

// 自研近似时间同步器
class ApproxSync
{
public:
    std::optional<std::pair<ImageMsg, LidarMsg>>
    feed(const ImageMsg &img, const LidarMsg &lidar, int64_t max_dt_ms = 50)
    {
        // 这里后续你可以填自己的同步逻辑
        return std::make_pair(img, lidar);
    }
};

} // namespace sensor_fusion_demo