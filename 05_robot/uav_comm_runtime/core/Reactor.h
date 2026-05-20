#ifndef REACTOR_H
#define REACTOR_H

#include <vector>
#include <queue>
#include <mutex>
#include <functional>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>

namespace uav_comm {

using Task = std::function<void()>;
constexpr size_t MAX_QUEUE_SIZE = 100000; // 背压上限（无人机安全阈值）

// 小顶堆定时器节点
struct TimerNode {
    long long expire_ts;  // 过期时间戳(ms)
    int interval;         // 执行间隔
    Task task;
    // 小顶堆比较：过期时间最小的优先
    bool operator>(const TimerNode& other) const {
        return expire_ts > other.expire_ts;
    }
};

/**
 * @brief 工业级Reactor
 * 单线程 | 小顶堆定时器(O(logN)) | 高性能队列 | 背压保护
 * 专为无人机/嵌入式设计：稳定、低延迟、无卡顿
 */
class Reactor {
public:
    static Reactor& instance() {
        static Reactor inst;
        return inst;
    }

    // 引用计数启动/停止
    void start() {
        std::lock_guard<std::mutex> lock(mtx_);
        ref_count_++;
        if (!running_) {
            running_ = true;
            loop_thread_ = std::thread(&Reactor::run, this);
        }
    }

    void stop() {
        std::lock_guard<std::mutex> lock(mtx_);
        // 防止计数变为负数（异常路径防护）
        if (ref_count_ > 0) {
            ref_count_--;
        }
        if (ref_count_ <= 0 && running_) {
            running_ = false;
            if (loop_thread_.joinable()) loop_thread_.join();
        }
    }

    // 提交任务（带背压保护，防止队列溢出）
    bool post(Task task) {
        std::lock_guard<std::mutex> lock(q_mtx_);
        if (task_queue_.size() >= MAX_QUEUE_SIZE) {
            return false; // 背压触发，拒绝任务（无人机安全机制）
        }
        task_queue_.push(std::move(task));
        return true;
    }

    // 添加循环定时任务
    void set_interval(int interval_ms, Task task) {
        std::lock_guard<std::mutex> lock(timer_mtx_);
        TimerNode node;
        node.expire_ts += interval_ms;
        node.interval = interval_ms;
        node.task = std::move(task);
        timer_heap_.push(std::move(node));
    }

private:
    Reactor() = default;
    ~Reactor() {
        stop();
    }

    std::atomic<bool> running_;
    std::thread loop_thread_;

    // 高性能任务队列（单消费场景最优）
    std::queue<Task> task_queue_;
    std::mutex q_mtx_;

    // 小顶堆定时器（O(logN)，无遍历卡顿）
    std::priority_queue<TimerNode, std::vector<TimerNode>, std::greater<TimerNode>> timer_heap_;
    std::mutex timer_mtx_;

    std::atomic<int> ref_count_{0};
    std::mutex mtx_;

    // 主循环（无无效sleep，纯时间驱动）
    void run() {
        while (running_) {
            process_tasks();   // 执行任务
            process_timers();  // 执行定时
            std::this_thread::sleep_for(std::chrono::microseconds(100)); // 极低开销
        }
    }

    // 批量执行队列任务
    void process_tasks() {
        std::queue<Task> tmp;
        {
            std::lock_guard<std::mutex> lock(q_mtx_);
            tmp.swap(task_queue_);
        }

        while (!tmp.empty()) {
            auto t = tmp.front();
            tmp.pop();
            if (t) t();
        }
    }

    // 小顶堆定时器执行（工业级标准实现）
    void process_timers() {
        std::lock_guard<std::mutex> lock(timer_mtx_);
        long long cur = now();

        while (!timer_heap_.empty()) {
            const auto& top = timer_heap_.top();
            if (top.expire_ts > cur) break;

            // 执行到期任务
            TimerNode node = timer_heap_.top();
            timer_heap_.pop();
            if (node.task) node.task();

            // 重新加入循环任务
            node.expire_ts += node.interval;
            timer_heap_.push(std::move(node));
        }
    }

    // 获取毫秒时间戳
    long long now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace uav_comm

#endif