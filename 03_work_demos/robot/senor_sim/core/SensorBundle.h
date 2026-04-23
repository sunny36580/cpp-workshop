#pragma once
#include <optional>
#include "Frame.h"

// 传感器融合数据包（支持单传感器/双传感器/无数据）
struct SensorBundle {
    std::optional<ImageFrame> image;
    std::optional<LidarFrame> lidar;
};