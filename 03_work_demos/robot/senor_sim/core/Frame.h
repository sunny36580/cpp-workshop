#pragma once
#include <string>
#include <cstdint>

// 基础数据帧
struct Frame {
    int64_t ts;        // 时间戳
    std::string frame_id;
};

// 相机图像帧
struct ImageFrame : Frame {
    int id = 0;        // 帧序号
};

// 激光雷达帧
struct LidarFrame : Frame {
    int id = 0;        // 帧序号
};