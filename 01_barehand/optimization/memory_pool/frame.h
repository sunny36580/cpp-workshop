#pragma once
#include <cstddef>

constexpr size_t WIDTH = 1920;
constexpr size_t HEIGHT = 1080;

// 模拟一帧（~2MB）
struct Frame {
    alignas(64) char data[WIDTH * HEIGHT];  // cache line 对齐（小优化）
};