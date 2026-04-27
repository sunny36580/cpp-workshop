#pragma once
#include <cstdint>
#include <memory>

struct Frame {
    int64_t ts = 0;
    int id = 0;
};

struct ImageFrame : public Frame {};
struct LidarFrame : public Frame {};

// 新增
using ImageFramePtr = std::shared_ptr<ImageFrame>;
using LidarFramePtr = std::shared_ptr<LidarFrame>;