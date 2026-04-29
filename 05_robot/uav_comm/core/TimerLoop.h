#ifndef TIMERLOOP_H
#define TIMERLOOP_H
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

namespace uav_comm {

class TimerLoop {
public:
    TimerLoop() = default;
    ~TimerLoop() { stop(); }

    void start() {
        if (running_) return;
        running_ = true;
        th_ = std::thread([this] { run(); });
    }

    void stop() {
        running_ = false;
        if (th_.joinable()) th_.join();
    }

    void add_task(int interval_ms, std::function<void()> cb) {
        std::lock_guard<std::mutex> lock(mtx_);
        tasks_.push_back({interval_ms, now(), std::move(cb)});
    }

private:
    struct Task {
        int interval;
        long long last_run;
        std::function<void()> callback;
    };

    std::vector<Task> tasks_;
    std::mutex mtx_;
    std::atomic<bool> running_{false};
    std::thread th_;

    void run() {
        while (running_) {
            auto cur = now();
            {
                std::lock_guard<std::mutex> lock(mtx_);
                for (auto& t : tasks_) {
                    if (cur - t.last_run >= t.interval) {
                        t.callback();
                        t.last_run = cur;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    long long now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

} // namespace

#endif // TIMERLOOP_H