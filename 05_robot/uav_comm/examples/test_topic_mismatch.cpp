#include "uav_comm/uav_comm.h"
#include "pose.pb.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>

using namespace uav_comm;

std::atomic<bool> g_running{true};

void sig_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, sig_handler);

    auto transport = UavTransport::create();
    UavNode node(transport, "test_node", 0);

    QoS qos;
    qos.reliability = Reliability::RELIABLE;

        // 发布：uav/pose
    auto pub = node.create_publisher("uav/pose", qos);
        // 订阅：uav/status  不一样 → 不匹配
    auto sub = node.create_subscription("uav/status",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "[ERROR] 不该收到消息！" << std::endl;
        }
        , qos);

    int i = 0;
    while (g_running) {
        Pose p;
        p.set_x(i++);
        std::string bytes;
        p.SerializeToString(&bytes);
        std::vector<uint8_t> data(bytes.begin(), bytes.end());

        pub->publish(data);
        std::cout << "[PUB] send (topic mismatch, no recv)\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}