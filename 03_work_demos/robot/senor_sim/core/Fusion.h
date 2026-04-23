#pragma once
#include "SensorBundle.h"
#include "Tracker.h"
#include "../utils/Logger.h"

// 融合层：输出稳定跟踪目标（替代原始传感器数据）
class Fusion {
public:
    void process(const std::vector<Track>& tracks) {
        if (tracks.empty()) {
            LOG_ERROR("Fallback: No targets (prediction failed)");
            return;
        }

        for (const auto& t : tracks) {
            std::string mode = t.is_predicted ? "[PREDICT]" : "[NORMAL]";
            LOG_INFO("Track ID: " << t.track_id << " | X: " << t.x << " Y: " << t.y << " " << mode);
        }
    }
};