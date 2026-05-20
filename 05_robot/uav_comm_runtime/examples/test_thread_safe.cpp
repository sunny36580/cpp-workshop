#include "uav_comm/uav_comm.h"
#include "pose.pb.h"
#include <iostream>
#include <thread>
#include <csignal>
#include <atomic>
#include <chrono>
#include <iomanip>

using namespace uav_comm;
using namespace std::chrono;

std::atomic<bool> g_running{true};
// 总接收计数
std::atomic<uint64_t> g_recv_count{0};
// 两个发送线程各自计数
std::atomic<uint64_t> g_send_count1{0};
std::atomic<uint64_t> g_send_count2{0};

std::atomic<uint64_t> g_total_latency{0};
std::atomic<uint32_t> g_latency_samples{0};

void sig_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, sig_handler);

    auto transport = UavTransport::create();
    UavNode node(transport, "thread_test_node", 0);

    QoS qos;
    qos.reliability = Reliability::BEST_EFFORT;
    const char* topic = "uav/thread/test";

    // 【核心】同一个 publisher，被两个线程同时调用 publish
    auto pub = node.create_publisher(topic, qos);

    // 订阅者
    auto sub = node.create_subscription(topic,
        [](const std::vector<uint8_t>& data) {
            g_recv_count++;
            uint64_t send_ts;
            if (data.size() >= sizeof(send_ts)) {
                memcpy(&send_ts, data.data(), sizeof(send_ts));
                auto now_ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
                g_total_latency += (now_ts - send_ts);
                g_latency_samples++;
            }
        }, qos);

    // 统计线程
    std::thread stat_thread([&]() {
        while (g_running) {
            uint64_t s1 = g_send_count1.exchange(0);
            uint64_t s2 = g_send_count2.exchange(0);
            uint64_t total_send = s1 + s2;
            uint64_t recv = g_recv_count.exchange(0);

            double avg_lat = 0;
            if (g_latency_samples > 0) {
                avg_lat = g_total_latency / (double)g_latency_samples / 1000.0;
                g_total_latency = 0;
                g_latency_samples = 0;
            }

            std::cout << "[双线程压测] "
                      << "线程1发送:" << std::setw(5) << s1 << " | "
                      << "线程2发送:" << std::setw(5) << s2 << " | "
                      << "总发送:" << std::setw(6) << total_send << " | "
                      << "总接收:" << std::setw(6) << recv << " | "
                      << "延迟:" << avg_lat << "ms"
                      << std::endl;

            std::this_thread::sleep_for(seconds(1));
        }
    });

    // ==============================================
    // 【关键测试】线程 1 并发 publish
    // ==============================================
    std::thread send_thread1([&]() {
        Pose p;
        p.set_x(111);
        std::string ser;
        while (g_running) {
            uint64_t ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
            p.SerializeToString(&ser);
            std::vector<uint8_t> msg(sizeof(ts) + ser.size());
            memcpy(msg.data(), &ts, sizeof(ts));
            memcpy(msg.data() + sizeof(ts), ser.data(), ser.size());

            // 多线程并发调用 publish
            pub->publish(std::move(msg));
            g_send_count1++;
        }
    });

    // ==============================================
    // 【关键测试】线程 2 同时 publish（同一个 pub 对象）
    // ==============================================
    std::thread send_thread2([&]() {
        Pose p;
        p.set_x(222);
        std::string ser;
        while (g_running) {
            uint64_t ts = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
            p.SerializeToString(&ser);
            std::vector<uint8_t> msg(sizeof(ts) + ser.size());
            memcpy(msg.data(), &ts, sizeof(ts));
            memcpy(msg.data() + sizeof(ts), ser.data(), ser.size());

            // 双线程同时 publish！
            pub->publish(std::move(msg));
            g_send_count2++;
        }
    });

    // 等待所有线程
    stat_thread.join();
    send_thread1.join();
    send_thread2.join();

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}