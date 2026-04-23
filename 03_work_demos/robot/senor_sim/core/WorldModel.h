#pragma once
#include <vector>
#include "Tracker.h"

// 世界模型：给上层飞控/决策的唯一数据结构
struct WorldModel {
    std::vector<Track> fused_objects;  // 稳定目标
    std::string system_state;          // 系统状态
    bool camera_alive = false;
    bool lidar_alive = false;
};