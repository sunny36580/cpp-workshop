#include "uav_comm/uav_comm.h"
#include "pose.pb.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>
#include <chrono>

using namespace uav_comm;
std::atomic<bool> g_running{true};

void sig_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, sig_handler);
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    auto transport = UavTransport::create();
    // 🔥 全部用同一个节点，TopicGraph 共享，所有匹配立刻生效
    UavNode node(transport, "test_node", 0);

    QoS qos;
    qos.reliability = Reliability::RELIABLE;

    // ==========================
    // 两个发布者（同一个节点）
    // ==========================
    auto pub_pose_1 = node.create_publisher("uav/pose/1", qos);
    auto pub_pose_2 = node.create_publisher("uav/pose/2", qos);

    // ==========================
    // 精准匹配1/2
    // ==========================
    auto sub_exact_1 = node.create_subscription("uav/pose/1",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "\033[32m[精准1] uav/pose/1 → x=" << p.x() << "\033[0m" << std::endl;
        }, qos);

    auto sub_exact_2 = node.create_subscription("uav/pose/2",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "\033[36m[精准2] uav/pose/2 → x=" << p.x() << "\033[0m" << std::endl;
        }, qos);

    // ==========================
    // 通配匹配
    // ==========================
    auto sub_wild_pose_all = node.create_subscription("uav/pose/*",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "\033[33m[通配] uav/pose/* → x=" << p.x() << "\033[0m" << std::endl;
        }, qos);

    auto sub_wild_uav_all = node.create_subscription("uav/**",
        [](const std::vector<uint8_t>& data) {
            Pose p;
            p.ParseFromArray(data.data(), data.size());
            std::cout << "\033[34m[全域] uav/** → x=" << p.x() << "\033[0m" << std::endl;
        }, qos);

    // ==========================
    // 循环发送
    // ==========================
    int count1 = 0, count2 = 0;
    while (g_running) {
        // 发1
        {
            Pose p;
            p.set_x(count1++);
            std::string ser;
            p.SerializeToString(&ser);
            std::vector<uint8_t> msg(ser.begin(), ser.end());
            std::cout << "\n[发布] uav/pose/1 x=" << p.x() << std::endl;
            pub_pose_1->publish(std::move(msg));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // 发2
        {
            Pose p;
            p.set_x(count2++);
            std::string ser;
            p.SerializeToString(&ser);
            std::vector<uint8_t> msg(ser.begin(), ser.end());
            std::cout << "\n[发布] uav/pose/2 x=" << p.x() << std::endl;
            pub_pose_2->publish(std::move(msg));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}