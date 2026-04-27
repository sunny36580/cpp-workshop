#pragma once
#include <deque>
#include <optional>
#include <mutex>
#include <cmath>
#include "Frame.h"
#include "SensorBundle.h"
#include "../utils/Time.h"

class ApproxSync {
public:
    static constexpr int64_t SYNC_TOLERANCE = 10;
    static constexpr int64_t EXPIRE_TIME = 200;
    static constexpr size_t MAX_BUFFER = 30;

    void add_image(ImageFramePtr f) {
        std::lock_guard<std::mutex> lock(mtx_);
        img_buf_.push_back(std::move(f));
        clean_buffer(img_buf_);
    }

    void add_lidar(LidarFramePtr f) {
        std::lock_guard<std::mutex> lock(mtx_);
        lidar_buf_.push_back(std::move(f));
        clean_buffer(lidar_buf_);
    }

    std::optional<SensorBundle> sync() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (img_buf_.empty() && lidar_buf_.empty()) return std::nullopt;

        SensorBundle bundle;
        if (!lidar_buf_.empty()) {
            auto lidar = lidar_buf_.front();
            auto best_img = find_closest(lidar->ts, img_buf_);

            if (best_img != img_buf_.end() && std::abs((*best_img)->ts - lidar->ts) < SYNC_TOLERANCE) {
                bundle.lidar = lidar;
                bundle.image = *best_img;
                lidar_buf_.pop_front();
                img_buf_.erase(best_img);
                return bundle;
            }

            bundle.lidar = lidar;
            lidar_buf_.pop_front();
            return bundle;
        }

        if (!img_buf_.empty()) {
            bundle.image = img_buf_.front();
            img_buf_.pop_front();
            return bundle;
        }
        return std::nullopt;
    }

private:
    std::deque<ImageFramePtr> img_buf_;
    std::deque<LidarFramePtr> lidar_buf_;
    std::mutex mtx_;

    template<typename T>
    typename std::deque<T>::iterator find_closest(int64_t ts, std::deque<T>& buf) {
        auto best = buf.end();
        int64_t min_dt = EXPIRE_TIME;
        for (auto it = buf.begin(); it != buf.end(); ++it) {
            int64_t dt = std::abs((*it)->ts - ts);
            if (dt < min_dt) { min_dt = dt; best = it; }
        }
        return best;
    }

    template<typename T>
    void clean_buffer(std::deque<T>& buf) {
        int64_t cur = now_ms();
        while (!buf.empty() && cur - buf.front()->ts > EXPIRE_TIME) buf.pop_front();
        while (buf.size() > MAX_BUFFER) buf.pop_front();
    }
};