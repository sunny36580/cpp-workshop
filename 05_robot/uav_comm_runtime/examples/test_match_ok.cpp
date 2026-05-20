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

    // 同一个 domain
    auto transport = UavTransport::create();
    UavNode node(transport, "test_node", 0);

    QoS qos_pub;
    qos_pub.reliability = Reliability::RELIABLE;

    QoS qos_sub;
    qos_sub.reliability = Reliability::RELIABLE;

    auto pub = node.create_publisher("uav/pose", qos_pub);
    auto sub = node.create_subscription("uav/pose",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "[正常匹配] recv: " << p.x() << std::endl;
        }, qos_sub);

    int i = 0;
    while (g_running) {
        Pose p;
        p.set_x(i++);
        std::string bytes;
        p.SerializeToString(&bytes);
        std::vector<uint8_t> data(bytes.begin(), bytes.end());

        std::cout << "[正常匹配] send: " << p.x() << std::endl;
        pub->publish(std::move(data));
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}