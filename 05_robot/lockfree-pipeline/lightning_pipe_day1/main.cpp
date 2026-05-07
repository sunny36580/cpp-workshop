#include <iostream>
#include <queue>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>

using namespace std;
using namespace chrono;

// ===================== 1. 数据结构定义 =====================
struct Frame {
    int id;                 // 帧编号
    uint64_t ts;            // 时间戳(微秒)
    float simulate_data;    // 模拟感知数据
};

// ===================== 2. 线程安全队列(有界队列 + 背压) =====================
template<typename T>
class SafeQueue {
public:
    // 构造：指定队列最大容量（有界队列）, 无界队列必然导致延迟爆炸、内存爆炸
    explicit SafeQueue(size_t max_size) : max_size_(max_size) {}

    void push(T&& v) {
        lock_guard<mutex> lk(mtx_);
        // 工业级背压：队列满了 → 丢弃最旧的帧，保证最新帧能入队。背压是系统生命线
        while (q_.size() >= max_size_) {
            q_.pop();
        }
        q_.push(move(v));
    }

    bool try_pop(T& v) {
        lock_guard<mutex> lk(mtx_);
        if (q_.empty()) return false;
        v = move(q_.front());
        q_.pop();
        return true;
    }

private:
    queue<T> q_;
    mutex mtx_;
    size_t max_size_; // 队列容量上限（解决堆积）
};

// 核心：设置队列容量=10（小容量，防止堆积，延迟极低），队列多大会导致延迟增大
SafeQueue<Frame> q1{10};  // Simulator -> Detect
SafeQueue<Frame> q2{10};  // Detect -> Sink

// 全局退出标志
atomic<bool> g_running{true};

// ===================== 3. 线程1：数据模拟器 =====================
void simulator_thread() {
    int frame_id = 0;
    while (g_running) {
        // 生成模拟帧
        Frame frame;
        frame.id = frame_id++;
        frame.ts = duration_cast<microseconds>(
            system_clock::now().time_since_epoch()
        ).count();
        frame.simulate_data = (float)rand() / RAND_MAX;

        // 推入队列
        q1.push(move(frame));
        // 调整：生产速度 ≈ 消费速度（1ms发1帧）
        // this_thread::sleep_for(microseconds(1000));
        // 上游生产过多，下游无法及时消费，导致堆积，延迟增大
        this_thread::sleep_for(microseconds(2000));
    }
}

// ===================== 4. 线程2：检测模块(AI/算法处理) =====================
// 检测耗时慢，上游生产快，下游消费慢，导致堆积，延迟增大
void detect_thread() {
    Frame frame;
    while (g_running) {
        if (q1.try_pop(frame)) {
            // 模拟算法耗时 1ms
            this_thread::sleep_for(milliseconds(1));

            // 推入下一级队列
            q2.push(move(frame));
        }
    }
}

// ===================== 5. 线程3：数据Sink(统计FPS+延迟) =====================
void sink_thread() {
    int count = 0;
    uint64_t total_latency = 0;
    auto last_time = system_clock::now();

    Frame frame;
    while (g_running) {
        if (q2.try_pop(frame)) {
            // 计算延迟(微秒 → 毫秒)
            auto now_us = duration_cast<microseconds>(
                system_clock::now().time_since_epoch()
            ).count();
            float latency_ms = (now_us - frame.ts) / 1000.0f;

            count++;
            total_latency += (now_us - frame.ts);

            // 每秒打印一次统计
            auto now = system_clock::now();
            if (duration_cast<seconds>(now - last_time).count() >= 1) {
                float avg_latency = (total_latency / 1000.0f) / count;
                cout << "=====================================" << endl;
                cout << "FPS: " << count << endl;
                cout << "平均延迟: " << avg_latency << " ms" << endl;
                cout << "=====================================" << endl;

                // 重置统计
                count = 0;
                total_latency = 0;
                last_time = now;
            }
        }
    }
}

// ===================== 主函数 =====================
int main() {
    cout << "Day1 Pipeline 启动 (Mutex版 3线程)..." << endl;

    // 启动3个线程
    thread sim_thread(simulator_thread);
    thread det_thread(detect_thread);
    thread sk_thread(sink_thread);

    // 运行10秒后自动退出
    this_thread::sleep_for(seconds(30));
    g_running = false;

    // 等待线程结束
    sim_thread.join();
    det_thread.join();
    sk_thread.join();

    cout << "Pipeline 退出" << endl;
    return 0;
}