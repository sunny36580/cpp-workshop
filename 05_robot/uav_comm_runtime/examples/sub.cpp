#include "uav_comm/uav_comm.h"
#include "pose.pb.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>

using namespace uav_comm;

std::atomic<bool> g_running{true};

void sig(int) { g_running = false; }

int main() {
    signal(SIGINT, sig);

    // 修复
    auto transport = UavTransport::create();
    UavNode node(transport, "node2", 0);

    QoS qos;
    qos.depth = 5;
    qos.reliability = Reliability::RELIABLE;

    auto sub = node.create_subscription(
        "uav/pose",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "[SUB node2] recv: " << p.x() << std::endl;
        }, qos
    );

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}