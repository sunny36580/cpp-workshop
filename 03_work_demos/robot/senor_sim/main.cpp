#include "sensor/CameraSensor.h"
#include "sensor/LidarSensor.h"
#include "core/Sync.h"
#include "core/Fusion.h"
#include "core/Monitor.h"
#include "core/Tracker.h"
#include "core/Preprocess.h"
#include "core/WorldModel.h"
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

    Tracker tracker;
    ObjectFusion object_fusion;
    const float dt = 0.02f;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> lock(mtx);

        // 1. 同步
        auto bundle = sync.sync();
        if (!bundle) continue;

        // 2. 传感器监控
        bool cam_ok = monitor.is_alive("camera", 200);
        bool lidar_ok = monitor.is_alive("lidar", 300);
        if (!cam_ok) LOG_ERROR("Camera DEAD");
        if (!lidar_ok) LOG_ERROR("Lidar DEAD");

        // 3. 预处理
        if (!Preprocessor::process(*bundle)) continue;

        // 4. 检测
        std::vector<Detection> dets;
        if (bundle->image || bundle->lidar) {
            dets.push_back({3.1f + (rand()%100)/100.0f, 2.5f + (rand()%100)/100.0f});
        }

        // 5. 跟踪 + 卡尔曼
        tracker.update(dets, dt);

        // 6. 目标级融合 + 世界模型
        WorldModel model = object_fusion.fuse(tracker.get_tracks(), cam_ok, lidar_ok);
    }

    LOG_INFO("System stopped safely");
    return 0;
}