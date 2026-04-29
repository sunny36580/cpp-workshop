// main.cc
#include "transport/message_bus.h"
#include "tools/logger.h"

struct ImuData {
    double ts;
    float acc[3] = {0};
};

int main() {
    Executor executor;
    MessageBus bus(executor);

    // 订阅：融合模块
    bus.subscribe("sensor/imu", "fusion",[](std::shared_ptr<void> data) {
        auto imu = std::static_pointer_cast<ImuData>(data);
        LOG_INFO("Fusion recv IMU: ts=" << imu->ts);
    });

    // 发布：感知模块
    auto imu_data = std::make_shared<ImuData>();
    imu_data->ts = 1.0;
    imu_data->acc[0] = 9.8;

    LOG_INFO("Perception publish IMU");
    bus.publish("sensor/imu", imu_data);

    return 0;
}