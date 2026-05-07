#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstddef>

using namespace std;
using namespace chrono;

// ===================== 1. 数据结构定义（不变） =====================
struct Frame {
    int id;
    uint64_t ts;
    float simulate_data;
};

// ===================== 2. Day2 核心：SPSC 无锁队列（工业极简版） =====================
// 单生产者单消费者 -> 感知管线专用！性能碾压 Mutex 队列
template<typename T, size_t Capacity = 16>  // 容量固定，有界防堆积
class SPSCQueue {
    // 缓存行对齐（64字节）：消除伪共享，性能暴涨关键
    struct alignas(64) AtomicIndex {
        atomic<size_t> val{0};
    };

public:
    SPSCQueue() = default;

    // 生产者入队（单线程调用）
    bool enqueue(T&& val) {
        const auto write = write_idx_.val.load(memory_order_relaxed);
        const auto next = (write + 1) % Capacity;

        // 队列满 -> 直接返回（背压：丢弃旧帧，保新帧）
        if (next == read_idx_.val.load(memory_order_acquire)) {
            return false;
        }

        buffer_[write] = move(val);
        write_idx_.val.store(next, memory_order_release);
        return true;
    }

    // 消费者出队（单线程调用）
    bool dequeue(T& val) {
        const auto read = read_idx_.val.load(memory_order_relaxed);

        // 队列空
        if (read == write_idx_.val.load(memory_order_acquire)) {
            return false;
        }

        val = move(buffer_[read]);
        read_idx_.val.store((read + 1) % Capacity, memory_order_release);
        return true;
    }

private:
    T buffer_[Capacity];
    AtomicIndex read_idx_;   // 消费者索引
    AtomicIndex write_idx_;  // 生产者索引
};

// ===================== 全局队列：替换为 SPSC 无锁队列 =====================
SPSCQueue<Frame, 16> q1;  // Simulator(单) → Detect(单)
SPSCQueue<Frame, 16> q2;  // Detect(单) → Sink(单)

atomic<bool> g_running{true};

// ===================== 3. 线程逻辑（和Day1完全一致，无修改） =====================
void simulator_thread() {
    int frame_id = 0;
    while (g_running) {
        Frame frame;
        frame.id = frame_id++;
        frame.ts = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
        frame.simulate_data = (float)rand() / RAND_MAX;

        q1.enqueue(move(frame));
        this_thread::sleep_for(microseconds(2000));
    }
}

void detect_thread() {
    Frame frame;
    while (g_running) {
        if (q1.dequeue(frame)) {
            this_thread::sleep_for(milliseconds(1));  // 模拟算法耗时
            q2.enqueue(move(frame));
        }
    }
}

void sink_thread() {
    int count = 0;
    uint64_t total_latency = 0;
    auto last_time = system_clock::now();

    Frame frame;
    while (g_running) {
        if (q2.dequeue(frame)) {
            auto now_us = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
            float latency_ms = (now_us - frame.ts) / 1000.0f;

            count++;
            total_latency += (now_us - frame.ts);

            if (duration_cast<seconds>(system_clock::now() - last_time).count() >= 1) {
                float avg_latency = (total_latency / 1000.0f) / count;
                cout << "=====================================" << endl;
                cout << "FPS: " << count << endl;
                cout << "平均延迟: " << avg_latency << " ms" << endl;
                cout << "=====================================" << endl;

                count = 0;
                total_latency = 0;
                last_time = system_clock::now();
            }
        }
    }
}

// ===================== 主函数（不变） =====================
int main() {
    cout << "Day2 SPSC无锁版 Pipeline 启动..." << endl;

    thread sim_thread(simulator_thread);
    thread det_thread(detect_thread);
    thread sk_thread(sink_thread);

    this_thread::sleep_for(seconds(10));
    g_running = false;

    sim_thread.join();
    det_thread.join();
    sk_thread.join();

    cout << "Pipeline 退出" << endl;
    return 0;
}