#include "uav_comm/uav_comm.h"
#include "pose.pb.h"
#include <thread>
#include <iostream>
#include <csignal>
#include <atomic>

using namespace uav_comm;

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

int main() {
    signal(SIGINT, signal_handler);

    auto transport = UavTransport::create();
    UavNode node(transport, "node1", 0);

    int i = 0;
    QoS qos;
    qos.reliability = Reliability::RELIABLE;
    auto pub = node.create_publisher("uav/pose", qos);

    while (g_running) {
        Pose p;
        p.set_x(i++);
        std::string bytes;
        p.SerializeToString(&bytes);
        std::vector<uint8_t> data(bytes.begin(), bytes.end());

        std::cout << "[PUB node1] send x=" << p.x() << std::endl;
        pub->publish(data);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}