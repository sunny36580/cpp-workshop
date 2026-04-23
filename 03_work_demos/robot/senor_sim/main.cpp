#include "sensor/CameraSensor.h"
#include "sensor/LidarSensor.h"
#include "core/Sync.h"
#include "core/Fusion.h"
#include "core/Monitor.h"
#include "core/Tracker.h"
#include <mutex>
#include <csignal>
#include <atomic>
#include <iostream>

// 全局运行标志
std::atomic<bool> g_running = true;

// Ctrl+C 信号处理
void signal_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, signal_handler);
    LOG_INFO("Sensor fusion system started...");

    CameraSensor cam;
    LidarSensor lidar;
    ApproxSync sync;
    Fusion fusion;
    SensorMonitor monitor;
    std::mutex mtx;

    // 相机数据回调
    cam.start([&](const ImageFrame& f) {
        std::lock_guard<std::mutex> lock(mtx);
        monitor.update("camera");
        sync.add_image(f);
    });

    // 雷达数据回调
    lidar.start([&](const LidarFrame& f) {
        std::lock_guard<std::mutex> lock(mtx);
        monitor.update("lidar");
        sync.add_lidar(f);
    });

    // 初始化跟踪器（新增）
    Tracker tracker;
    const float dt = 0.02f;  // 20ms 步长

    // 主循环：完整工程级感知Pipeline
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> lock(mtx);

        // ===================== 原有模块 =====================
        // 1. 时间同步
        auto bundle = sync.sync();
        if (!bundle) continue;

        // 掉线告警（原有）
        if (!monitor.is_alive("camera", 200)) LOG_ERROR("Camera sensor DEAD!");
        if (!monitor.is_alive("lidar", 300)) LOG_ERROR("Lidar sensor DEAD!");

        // ===================== 新增智能层 =====================
        // 2. 模拟目标检测（对接传感器数据）
        std::vector<Detection> detections;
        if (bundle->image || bundle->lidar) {
            // 模拟2D检测目标（真实项目替换为AI检测）
            detections.push_back({3.1f + (rand()%100)/100.0f, 2.5f + (rand()%100)/100.0f});
        }

        // 3. 多目标跟踪 + 卡尔曼预测（抗掉线核心）
        tracker.update(detections, dt);

        // 4. 融合输出（给上层的稳定数据）
        fusion.process(tracker.get_tracks());
    }

    LOG_INFO("System stopped safely");
    return 0;
}