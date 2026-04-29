#ifndef EPOLL_REACTOR_H
#define EPOLL_REACTOR_H

#include <sys/epoll.h>
#include <unistd.h>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include "LockFreeQueue.h"

namespace uav_comm {

using Task = std::function<void()>;
constexpr int MAX_EVENTS = 1024;
constexpr int EPOLL_TIMEOUT_MS = 1;

// epoll + 无锁队列 事件循环
class EpollReactor {
public:
    static EpollReactor& instance() {
        static EpollReactor inst;
        return inst;
    }

    EpollReactor() : running_(false) {
        // 创建 epoll 实例
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) {
            perror("epoll_create failed");
        }
    }

    ~EpollReactor() {
        stop();
        close(epoll_fd_);
    }

    // 启动事件循环
    void start() {
        if (running_ || epoll_fd_ < 0) return;
        running_ = true;
        loop_thread_ = std::thread(&EpollReactor::run_loop, this);
    }

    // 停止事件循环
    void stop() {
        running_ = false;
        if (loop_thread_.joinable()) loop_thread_.join();
    }

    // 无锁提交任务
    void post(Task task) {
        task_queue_.push(std::move(task));
    }

    // 添加定时任务
    void set_interval(int interval_ms, Task task) {
        std::lock_guard<std::mutex> lock(timer_mtx_);
        timers_.emplace_back(TimerTask{interval_ms, now(), std::move(task)});
    }

private:
    struct TimerTask {
        int interval;
        long long last_run;
        Task cb;
    };

    int epoll_fd_ = -1;
    std::atomic<bool> running_{false};
    std::thread loop_thread_;

    // 无锁任务队列（核心优化）
    LockFreeQueue<Task> task_queue_;

    // 定时任务
    std::vector<TimerTask> timers_;
    std::mutex timer_mtx_;

    // epoll 主循环（无 sleep，纯事件驱动）
    void run_loop() {
        std::vector<epoll_event> events(MAX_EVENTS);

        while (running_) {
            // epoll 等待事件（阻塞等待，无空转）
            int n = epoll_wait(epoll_fd_, events.data(), MAX_EVENTS, EPOLL_TIMEOUT_MS);

            // 执行所有无锁队列任务
            process_tasks();

            // 执行定时任务
            process_timers();
        }
    }

    // 执行无锁队列所有任务
    void process_tasks() {
        Task task;
        while (task_queue_.pop(&task)) {
            if (task) task();
        }
    }

    // 执行定时任务
    void process_timers() {
        auto cur = now();
        std::lock_guard<std::mutex> lock(timer_mtx_);

        for (auto& t : timers_) {
            if (cur - t.last_run >= t.interval) {
                t.cb();
                t.last_run = cur;
            }
        }
    }

    // 获取当前时间戳（ms）
    long long now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace uav_comm

#endif