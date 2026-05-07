#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <queue>

using namespace std;
using namespace chrono;

// ======================================================
// Frame
// ======================================================

struct Frame {
    uint64_t id;
    uint64_t ts;
};

// ======================================================
// Latency Stats
// ======================================================

class LatencyStats {
public:
    void add(double us) {
        latencies_.push_back(us);
    }

    void print() {
        if (latencies_.empty()) return;

        sort(latencies_.begin(), latencies_.end());

        auto avg =
            accumulate(latencies_.begin(),
                       latencies_.end(),
                       0.0)
            / latencies_.size();

        cout << "\n==============================\n";
        cout << "count : " << latencies_.size() << endl;
        cout << "avg   : " << avg << " us" << endl;
        cout << "p50   : " << percentile(50) << " us" << endl;
        cout << "p95   : " << percentile(95) << " us" << endl;
        cout << "p99   : " << percentile(99) << " us" << endl;
        cout << "p999  : " << percentile(99.9) << " us" << endl;
        cout << "max   : " << latencies_.back() << " us" << endl;
        cout << "==============================\n";
    }

private:
    double percentile(double p) {
        size_t idx =
            static_cast<size_t>(
                (p / 100.0) * latencies_.size()
            );

        idx = min(idx, latencies_.size() - 1);

        return latencies_[idx];
    }

private:
    vector<double> latencies_;
};

// ======================================================
// Mutex Queue
// ======================================================

template<typename T>
class MutexQueue {
public:
    explicit MutexQueue(size_t cap)
        : cap_(cap) {}

    bool enqueue(T&& v) {
        lock_guard<mutex> lk(mtx_);

        if (q_.size() >= cap_) {
            return false;
        }

        q_.push(move(v));
        return true;
    }

    bool dequeue(T& v) {
        lock_guard<mutex> lk(mtx_);

        if (q_.empty()) {
            return false;
        }

        v = move(q_.front());
        q_.pop();

        return true;
    }

private:
    queue<T> q_;
    mutex mtx_;
    size_t cap_;
};

// ======================================================
// SPSC LockFree Queue
// ======================================================

template<typename T, size_t Capacity = 4096>
class SPSCQueue {

    struct alignas(64) AtomicIndex {
        atomic<size_t> val{0};
    };

public:

    bool enqueue(T&& val) {

        auto write =
            write_idx_.val.load(memory_order_relaxed);

        auto next = (write + 1) % Capacity;

        if (next ==
            read_idx_.val.load(memory_order_acquire)) {
            return false;
        }

        buffer_[write] = move(val);

        write_idx_.val.store(
            next,
            memory_order_release
        );

        return true;
    }

    bool dequeue(T& val) {

        auto read =
            read_idx_.val.load(memory_order_relaxed);

        if (read ==
            write_idx_.val.load(memory_order_acquire)) {
            return false;
        }

        val = move(buffer_[read]);

        read_idx_.val.store(
            (read + 1) % Capacity,
            memory_order_release
        );

        return true;
    }

private:
    T buffer_[Capacity];

    AtomicIndex read_idx_;
    AtomicIndex write_idx_;
};

// ======================================================
// 切换测试对象
// ======================================================

// using QueueType = MutexQueue<Frame>;
using QueueType = SPSCQueue<Frame, 4096>;

QueueType q;

// ======================================================
// Globals
// ======================================================

atomic<bool> running{true};

atomic<uint64_t> total_ops{0};
atomic<uint64_t> drop_count{0};

LatencyStats stats;

// ======================================================
// Producer
// ======================================================

void producer() {

    uint64_t id = 0;

    while (running) {

        Frame f;

        f.id = id++;

        f.ts =
            duration_cast<microseconds>(
                steady_clock::now()
                .time_since_epoch()
            ).count();

        if (!q.enqueue(move(f))) {
            drop_count++;
        }

        // 不 sleep !!!
        // 极限压测
    }
}

// ======================================================
// Consumer
// ======================================================

void consumer() {

    Frame f;

    while (running) {

        if (q.dequeue(f)) {

            auto now =
                duration_cast<microseconds>(
                    steady_clock::now()
                    .time_since_epoch()
                ).count();

            double latency =
                static_cast<double>(now - f.ts);

            stats.add(latency);

            total_ops++;

            // ==================================================
            // 调节这里！！！
            // ==================================================

            // 实验1:
            // 注释掉 -> 极限吞吐测试

            // 实验2:
            // 打开 -> 制造消费瓶颈
            // this_thread::sleep_for(microseconds(50));
        }
    }
}

// ======================================================
// Main
// ======================================================

int main() {

    cout << "====================================\n";
    cout << "Pipeline Benchmark Start\n";
    cout << "====================================\n";

    thread t1(producer);
    thread t2(consumer);

    auto start = steady_clock::now();

    this_thread::sleep_for(seconds(5));

    running = false;

    t1.join();
    t2.join();

    auto end = steady_clock::now();

    double sec =
        duration_cast<duration<double>>(end - start)
        .count();

    cout << "\n========== Throughput ==========\n";

    cout << "total ops  : "
         << total_ops.load()
         << endl;

    cout << "ops/sec    : "
         << total_ops.load() / sec
         << endl;

    cout << "drop count : "
         << drop_count.load()
         << endl;

    stats.print();

    return 0;
}
