#pragma once
#include "Sensor.h"
#include "../core/Frame.h"
#include "../utils/Time.h"
#include "../utils/Logger.h"
#include <thread>
#include <random>

// 相机传感器：30Hz + 时间抖动 + 模拟掉线
class CameraSensor : public Sensor<ImageFrame> {
public:
    void start(DataCallback cb) override {
        run_ = true;
        // 独立线程运行传感器
        sensor_thread_ = std::thread([this, cb]() {
            int frame_id = 0;
            // 全局随机数引擎（修复原代码抖动失效问题）
            static std::default_random_engine eng(std::random_device{}());
            std::uniform_int_distribution<int> jitter(-10, 10);

            while (run_) {
                // 30Hz = 33ms，添加±10ms抖动（时间不同步）
                std::this_thread::sleep_for(std::chrono::milliseconds(33 + jitter(eng)));

                // 模拟相机掉线（id 50~80 无数据）
                if (frame_id > 50 && frame_id < 80) {
                    LOG_WARN("Camera sensor dropped, skip frame: " << frame_id);
                    frame_id++;
                    continue;
                }

                // 生成有效数据
                ImageFrame frame;
                frame.ts = now_ms();
                frame.id = frame_id++;
                cb(frame);
            }
        });
    }

    ~CameraSensor() {
        run_ = false;
        if (sensor_thread_.joinable()) sensor_thread_.join();
    }

private:
    std::thread sensor_thread_;
    bool run_ = false;
};