#pragma once
#include <deque>
#include <optional>
#include <cmath>
#include "Frame.h"
#include "SensorBundle.h"

// 近似时间同步（核心）
class ApproxSync {
public:
    // 添加相机数据
    void add_image(const ImageFrame& f) { image_buf_.push_back(f); }
    // 添加雷达数据
    void add_lidar(const LidarFrame& f) { lidar_buf_.push_back(f); }

    // 同步匹配：时间差<50ms 则配对，否则单传感器降级
    std::optional<SensorBundle> sync() {
        if (image_buf_.empty() && lidar_buf_.empty()) return std::nullopt;

        SensorBundle bundle;
        if (!image_buf_.empty()) bundle.image = image_buf_.front();
        if (!lidar_buf_.empty()) bundle.lidar = lidar_buf_.front();

        // 双传感器时间匹配
        if (bundle.image && bundle.lidar) {
            const int64_t dt = std::abs(bundle.image->ts - bundle.lidar->ts);
            if (dt < 50) {
                image_buf_.pop_front();
                lidar_buf_.pop_front();
                return bundle;
            }
        }

        // 降级：单传感器输出
        if (bundle.image) { image_buf_.pop_front(); return bundle; }
        if (bundle.lidar) { lidar_buf_.pop_front(); return bundle; }

        return std::nullopt;
    }

private:
    std::deque<ImageFrame> image_buf_;
    std::deque<LidarFrame> lidar_buf_;
};