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

    // 使用对外接口创建 transport
    auto transport = UavTransport::create();

    // 使用 UavNode
    UavNode node_pub(transport, "pub_node", 0);
    UavNode node_sub(transport, "sub_node", 1);

    QoS qos;
    qos.reliability = Reliability::RELIABLE;

    auto pub = node_pub.create_publisher("uav/pose", qos);
    auto sub = node_sub.create_subscription("uav/pose", 
        [](const std::vector<uint8_t>&) {
            std::cout << "[ERROR] 域隔离失效！" << std::endl;
        },
        qos);

    int i = 0;
    while (g_running) {
        Pose p;
        p.set_x(i++);
        std::string bytes;
        p.SerializeToString(&bytes);
        std::vector<uint8_t> data(bytes.begin(), bytes.end());

        pub->publish(data);
        std::cout << "[PUB] send (domain isolation, no recv)\n";
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}