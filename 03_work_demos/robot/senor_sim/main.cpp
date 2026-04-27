#include "sensor/CameraSensor.h"
#include "sensor/LidarSensor.h"
#include "core/Sync.h"
#include "core/Fusion.h"
#include "core/Monitor.h"
#include "core/Tracker.h"
#include "core/Preprocess.h"
#include "core/WorldModel.h"
#include <csignal>
#include <atomic>
#include <iostream>

// 全局运行标志
std::atomic<bool> g_running = true;

// Ctrl+C 信号处理
void signal_handler(int) {
    g_running = false;
    LOG_INFO("收到退出信号，正在关闭系统...");
}

int main() {
    signal(SIGINT, signal_handler);
    LOG_INFO("===== 多传感器融合系统启动 =====");

    CameraSensor cam;
    LidarSensor lidar;
    ApproxSync sync;
    SensorMonitor monitor;
    Tracker tracker;
    ObjectFusion object_fusion;

    // 相机数据回调
    cam.start([&](ImageFramePtr f) {
        monitor.update("camera");
        sync.add_image(std::move(f));
    });

    // 雷达数据回调
    lidar.start([&](LidarFramePtr f) {
        monitor.update("lidar");
        sync.add_lidar(std::move(f));
    });

    const float dt = 0.02f;
    int print_counter = 0;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        // 1. 同步
        auto bundle = sync.sync();
        if (!bundle) continue;

        // 2. 传感器监控
        bool cam_ok = monitor.is_alive("camera", 200);
        bool lidar_ok = monitor.is_alive("lidar", 300);

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
        if (print_counter++ % 5 == 0) {
            WorldModel model = object_fusion.fuse(tracker.get_tracks(), cam_ok, lidar_ok);
        }
    }

    LOG_INFO("===== 系统安全退出 =====");
    return 0;
}