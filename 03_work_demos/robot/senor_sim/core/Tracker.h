#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include "Kalman.h"

// 检测目标（传感器输出的原始目标）
struct Detection {
    float x, y;
};

// 跟踪目标（输出给上层的稳定目标，带卡尔曼预测）
struct Track {
    int track_id;         // 全局唯一ID
    float x, y;           // 滤波后坐标
    bool is_predicted;    // 是否为预测值（传感器掉线时为true）
};

class Tracker {
public:
    // 更新跟踪器：预测 → 关联 → 更新 → 新建/删除
    void update(const std::vector<Detection>& detections, float dt) {
        // 1. 所有轨迹卡尔曼预测（掉线时核心：纯预测）
        for (auto& track : tracks_) {
            track.kf.predict(dt);
            track.is_predicted = true;
        }

        // 2. 最近邻数据关联
        std::vector<bool> used(detections.size(), false);
        for (auto& track : tracks_) {
            int best_idx = -1;
            float min_dist = 2.0f;  // 关联阈值

            for (int i = 0; i < detections.size(); ++i) {
                if (used[i]) continue;
                float d = hypot(detections[i].x - track.kf.x, detections[i].y - track.kf.y);
                if (d < min_dist) {
                    min_dist = d;
                    best_idx = i;
                }
            }

            // 匹配成功：更新卡尔曼，取消预测标记
            if (best_idx != -1) {
                track.kf.update(detections[best_idx].x, detections[best_idx].y);
                track.is_predicted = false;
                track.lost_count = 0;
                used[best_idx] = true;
            } else {
                // 匹配失败：丢失计数+1
                track.lost_count++;
            }
        }

        // 3. 新建轨迹
        for (int i = 0; i < detections.size(); ++i) {
            if (!used[i]) add_track(detections[i]);
        }

        // 4. 删除长期丢失的轨迹
        tracks_.erase(
            std::remove_if(tracks_.begin(), tracks_.end(),
                [](const TrackInternal& t) { return t.lost_count > 5; }),
            tracks_.end()
        );
    }

    // 获取当前所有稳定跟踪目标（给上层用）
    std::vector<Track> get_tracks() {
        std::vector<Track> res;
        for (const auto& t : tracks_) {
            res.push_back({t.track_id, t.kf.x, t.kf.y, t.is_predicted});
        }
        return res;
    }

private:
    // 内部跟踪结构（包含卡尔曼+状态）
    struct TrackInternal {
        int track_id;
        KalmanFilter kf;
        int lost_count = 0;
        bool is_predicted = false;
    };

    std::vector<TrackInternal> tracks_;
    int next_id_ = 1;

    // 添加新目标
    void add_track(const Detection& det) {
        TrackInternal t;
        t.track_id = next_id_++;
        t.kf.x = det.x;
        t.kf.y = det.y;
        tracks_.push_back(t);
    }
};