#ifndef THREAD_POOL_H
#define THREAD_POOL_H
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <vector>
#include <atomic>

namespace uav_comm {

class ThreadPool {
public:
    ThreadPool(int n = 3) : running_(true) {
        for (int i = 0; i < n; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mtx);
                        cv.wait(lock, [this] {
                            return !running_ || !tasks.empty();
                        });
                        if (!running_ && tasks.empty()) return;
                        if (tasks.empty()) continue;
                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        running_ = false;
        cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) t.join();
        }
    }

    void submit(std::function<void()> f) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            tasks.push(std::move(f));
        }
        cv.notify_one();
    }

private:
    std::atomic<bool> running_{true};
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
};

}

#endif // THREAD_POOL_H