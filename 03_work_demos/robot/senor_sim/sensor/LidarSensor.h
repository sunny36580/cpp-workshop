#pragma once
#include "Sensor.h"
#include "../core/Frame.h"
#include "../utils/Time.h"
#include <thread>

// 激光雷达：10Hz（稳定无抖动）
class LidarSensor : public Sensor<LidarFrame> {
public:
    void start(DataCallback cb) override {
        run_ = true;
        sensor_thread_ = std::thread([this, cb]() {
            int frame_id = 0;
            while (run_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 10Hz

                LidarFrame frame;
                frame.ts = now_ms();
                frame.id = frame_id++;
                cb(frame);
            }
        });
    }

    ~LidarSensor() {
        run_ = false;
        if (sensor_thread_.joinable()) sensor_thread_.join();
    }

private:
    std::thread sensor_thread_;
    bool run_ = false;
};