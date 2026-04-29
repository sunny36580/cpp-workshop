#include "uav_comm/uav_comm.h"
#include "pose.pb.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>

using namespace uav_comm;
using namespace std::chrono;
std::atomic<bool> g_running{true};
std::atomic<int> g_recv{0};

int main() {
    auto transport = UavTransport::create();
    UavNode node(transport, "multi_topic_node", 0);

    // 创建5个发布者 + 5个订阅者
    auto pub1 = node.create_publisher("uav/imu");
    auto pub2 = node.create_publisher("uav/gps");
    auto pub3 = node.create_publisher("uav/pose");
    auto pub4 = node.create_publisher("uav/status");
    auto pub5 = node.create_publisher("uav/cmd");

    // 订阅全部5个topic
    for(int i=0; i<5; i++) node.create_subscription("uav/*", [&](auto&){ g_recv++; });

    // 单线程发5个topic（真实场景用法）
    std::thread send([&](){
        Pose p; std::string s;
        while(g_running) {
            p.SerializeToString(&s); std::vector<uint8_t> d(s.begin(), s.end());
            pub1->publish(d); pub2->publish(d); pub3->publish(d); pub4->publish(d); pub5->publish(d);
        }
    });

    // 统计
    while(g_running) { std::cout << "多Topic接收: " << g_recv.exchange(0) << "\n"; std::this_thread::sleep_for(seconds(1)); }
    send.join(); return 0;
}