#pragma once
#include <vector>
#include <algorithm>
#include <cmath>
#include "Kalman.h"

struct Detection { float x, y; };
struct Track { int id; float x, y; bool is_predicted; };

class Tracker {
public:
    void update(const std::vector<Detection>& dets, float dt) {
        for (auto& t : tracks_) { t.kf.predict(dt); t.is_predicted = true; }

        std::vector<bool> used(dets.size(), false);
        for (auto& t : tracks_) {
            int best = -1;
            float min_d = 2.0f;
            for (int i=0; i<dets.size(); ++i) {
                if (used[i]) continue;
                float d = hypot(dets[i].x - t.kf.x, dets[i].y - t.kf.y);
                if (d < min_d) { min_d = d; best = i; }
            }
            if (best != -1) {
                t.kf.update(dets[best].x, dets[best].y);
                t.is_predicted = false;
                t.lost = 0;
                used[best] = true;
            } else t.lost++;
        }

        for (int i=0; i<dets.size(); ++i) if (!used[i]) add(dets[i]);
        tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
            [](auto& t) { return t.lost > 5; }), tracks_.end());
    }

    std::vector<Track> get_tracks() {
        std::vector<Track> res;
        for (auto& t : tracks_) res.push_back({t.id, t.kf.x, t.kf.y, t.is_predicted});
        return res;
    }

private:
    struct TrackInternal { int id; KalmanFilter kf; int lost=0; bool is_predicted=false; };
    std::vector<TrackInternal> tracks_;
    int next_id_ = 1;

    void add(const Detection& d) {
        TrackInternal t;
        t.id = next_id_++;
        t.kf.x = d.x;
        t.kf.y = d.y;
        tracks_.push_back(t);
    }
};