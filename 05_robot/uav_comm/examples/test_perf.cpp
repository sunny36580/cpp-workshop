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
std::atomic<uint64_t> g_recv_count{0};
std::atomic<uint64_t> g_send_count{0};

// 统计延迟
std::atomic<uint64_t> g_total_latency{0};
std::atomic<uint32_t> g_latency_samples{0};

void sig_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, sig_handler);
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    auto transport = UavTransport::create();
    UavNode node(transport, "perf_node", 0);

    // 压测用QoS：可切换 RELIABLE / BEST_EFFORT
    QoS qos;
    qos.reliability = Reliability::BEST_EFFORT; 
    // qos.reliability = Reliability::RELIABLE;

    // 固定主题
    const char* topic = "uav/perf/topic";
    auto pub = node.create_publisher(topic, qos);
    
    // 🔥 修复1：回调参数正确，无乱码
    auto sub = node.create_subscription(topic,
        [](const std::vector<uint8_t>& data) {
            g_recv_count++;

            // 解析时间戳，计算延迟
            uint64_t send_ts;
            if (data.size() >= sizeof(send_ts)) {
                memcpy(&send_ts, data.data(), sizeof(send_ts));
                auto now_ts = duration_cast<microseconds>(
                    steady_clock::now().time_since_epoch()).count();
                g_total_latency += (now_ts - send_ts);
                g_latency_samples++;
            }
        }, qos);

    // 🔥 修复2：lambda语法正确
    std::thread stat_thread([&]() {
        while (g_running) {
            uint64_t s = g_send_count.exchange(0);
            uint64_t r = g_recv_count.exchange(0);
            double avg_latency = 0;
            if (g_latency_samples > 0) {
                avg_latency = g_total_latency / (double)g_latency_samples / 1000.0;
                g_total_latency = 0;
                g_latency_samples = 0;
            }

            std::cout << "[性能统计] "
                      << "发送:" << std::setw(6) << s << " msg/s | "
                      << "接收:" << std::setw(6) << r << " msg/s | "
                      << "平均延迟:" << std::fixed << std::setprecision(2) << avg_latency << " ms"
                      << std::endl;

            std::this_thread::sleep_for(seconds(1));
        }
    });

    // 🔥 修复3：lambda语法正确
    std::thread send_thread([&]() {
        Pose p;
        p.set_x(123.456);
        p.set_y(789.123);
        std::string ser;

        while (g_running) {
            // 前8字节放发送时间戳，后面放protobuf
            uint64_t ts = duration_cast<microseconds>(
                steady_clock::now().time_since_epoch()).count();
            p.SerializeToString(&ser);

            std::vector<uint8_t> msg(sizeof(ts) + ser.size());
            memcpy(msg.data(), &ts, sizeof(ts));
            memcpy(msg.data() + sizeof(ts), ser.data(), ser.size());

            pub->publish(std::move(msg));
            g_send_count++;
        }
    });

    // 等待线程结束
    stat_thread.join();
    send_thread.join();

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}