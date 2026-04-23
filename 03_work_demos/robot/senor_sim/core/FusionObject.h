#pragma once
#include "Tracker.h"
#include "WorldModel.h"
#include "../utils/Logger.h"

class ObjectFusion {
public:
    WorldModel fuse(const std::vector<Track>& tracks, bool cam_ok, bool lidar_ok) {
        WorldModel model;
        model.fused_objects = tracks;
        model.camera_alive = cam_ok;
        model.lidar_alive = lidar_ok;

        if (cam_ok && lidar_ok)
            model.system_state = "NORMAL 双传感器融合";
        else if (cam_ok || lidar_ok)
            model.system_state = "DEGRADED 单传感器降级";
        else
            model.system_state = "FALLBACK 纯预测";

        print(model);
        return model;
    }

private:
    void print(const WorldModel& m) {
        LOG_INFO("===== World Model =====");
        LOG_INFO("状态: " << m.system_state);
        LOG_INFO("相机: " << (m.camera_alive ? "OK" : "DEAD")
                 << " 雷达: " << (m.lidar_alive ? "OK" : "DEAD"));
        for (auto& t : m.fused_objects) {
            LOG_INFO("ID:" << t.track_id << " (" << t.x << "," << t.y << ") "
                     << (t.is_predicted ? "[预测]" : "[正常]"));
        }
        LOG_INFO("=======================\n");
    }
};