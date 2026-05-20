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

    auto transport = UavTransport::create();
    UavNode node(transport, "test_node", 0);

    QoS qos_pub;
    qos_pub.reliability = Reliability::BEST_EFFORT; // 发布：尽力

    QoS qos_sub;
    qos_sub.reliability = Reliability::RELIABLE;     // 订阅：要求可靠

    auto pub = node.create_publisher("uav/pose", qos_pub);
    auto sub = node.create_subscription("uav/pose", 
        [](const std::vector<uint8_t>& data) {
            std::cout << "[❌ 错误！不应该出现] recv" << std::endl;
        }, qos_sub
       );

    int i = 0;
    while (g_running) {
        Pose p;
        p.set_x(i++);
        std::string bytes;
        p.SerializeToString(&bytes);
        std::vector<uint8_t> data(bytes.begin(), bytes.end());
        std::cout << "发送相关数据，qos不匹配，无接收" << std::endl;
        pub->publish(data);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}