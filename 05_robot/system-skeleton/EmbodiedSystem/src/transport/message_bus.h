#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

struct TopicGraphInfo {
    std::set<std::string> subscribers;
};

struct TopicChannel {
    std::type_index type{typeid(void)};

    std::vector<std::function<void(const void*)>> handlers;
    std::deque<std::function<void()>> tasks;
    std::mutex mtx;
    bool scheduled = false;
    size_t max_queue_size_ = 100;
};

class Executor {
   public:
    Executor(size_t worker_count = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < worker_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~Executor() {
        stop_.store(true, std::memory_order_release);
        cv_.notify_all();

        for (auto& w : workers_) w.join();
    }

    void schedule(TopicChannel* channel) {
        {
            std::lock_guard<std::mutex> lock(ready_mutex_);
            ready_topics_.push(channel);
        }

        cv_.notify_one();
    }

   private:
    void worker_loop() {
        while (!stop_.load(std::memory_order_acquire)) {
            TopicChannel* channel = nullptr;

            // 取出 ready_topics_ 队列里的 channel
            {
                std::unique_lock<std::mutex> lock(ready_mutex_);
                cv_.wait(lock, [this] { return stop_ || !ready_topics_.empty(); });

                if (stop_ && ready_topics_.empty())
                    return;

                channel = ready_topics_.front();
                ready_topics_.pop();
            }

            // 串行执行 channel 内的任务
            while (true) {
                std::function<void()> task;

                {
                    std::lock_guard<std::mutex> lock(channel->mtx);

                    if (channel->tasks.empty())
                        break;  // 队列空了就退出

                    task = std::move(channel->tasks.front());
                    channel->tasks.pop_front();
                }

                if (task)
                    task();
            }

            // 完全执行完 channel 队列后清除 scheduled 标记
            {
                std::lock_guard<std::mutex> lock(channel->mtx);

                if (channel->tasks.empty())
                    channel->scheduled = false;
                else
                    schedule(channel);  // 如果 publish 中有新任务，重新 schedule
            }
        }
    }

   private:
    std::queue<TopicChannel*> ready_topics_;
    std::mutex ready_mutex_;
    std::condition_variable cv_;

    std::vector<std::thread> workers_;
    std::atomic<bool> stop_{false};
};

class MessageBus {
   public:
    MessageBus(Executor& executor) : executor_(executor) {
    }

    template <typename T>
    void subscribe(const std::string& topic, const std::string& node_name, std::function<void(const T&)> handler) {
        graph_[topic].subscribers.insert(node_name);
        // 获取该消息类型对应的回调函数集合
        auto& channel = get_or_create_channel<T>(topic, typeid(T));

        std::lock_guard<std::mutex> lock(channel.mtx);
        // 类型擦除，数组中void(const void*)指针
        channel.handlers.emplace_back(
            [handler = std::move(handler)](const void* msg) { handler(*static_cast<const T*>(msg)); });
    }

    template <typename T>
    void subscribe(const std::string& topic,
                   const std::string& node_name,
                   std::function<void(std::shared_ptr<T>)> handler) {
        graph_[topic].subscribers.insert(node_name);

        auto& channel = get_or_create_channel<T>(topic, typeid(T));

        std::lock_guard<std::mutex> lock(channel.mtx);

        channel.handlers.emplace_back(
            [handler = std::move(handler)](const void* msg) { handler(*static_cast<const std::shared_ptr<T>*>(msg)); });
    }

    template <typename T>
    void publish(const std::string& topic, const T& msg) {
        auto* channel = get_channel<T>(topic);
        if (!channel)
            return;

        if (channel->type != std::type_index(typeid(T)))
            throw std::runtime_error("Topic type mismatch");

        std::function<void()> task;
        bool need_schedule = false;

        // ⭐ 改这里：用 shared_ptr
        auto msg_ptr = std::make_shared<T>(msg);

        {
            std::lock_guard<std::mutex> lock(channel->mtx);

            // ❗ 不再复制 handlers
            auto& handlers = channel->handlers;

            task = [handlers, msg_ptr]() {
                for (auto& handler : handlers) handler(&msg_ptr);
            };

            need_schedule = channel->tasks.empty() && !channel->scheduled;

            if (channel->tasks.size() >= channel->max_queue_size_)
                channel->tasks.pop_front();

            channel->tasks.emplace_back(std::move(task));

            if (need_schedule)
                channel->scheduled = true;
        }

        if (need_schedule)
            executor_.schedule(channel);
    }

    template <typename T>
    void publishPtr(const std::string& topic, std::shared_ptr<T> msg_ptr) {
        auto* channel = get_channel<T>(topic);
        if (!channel)
            return;

        std::function<void()> task;
        bool need_schedule = false;

        {
            std::lock_guard<std::mutex> lock(channel->mtx);

            auto& handlers = channel->handlers;

            task = [handlers, msg_ptr]() {
                for (auto& handler : handlers) handler(&msg_ptr);
            };

            need_schedule = channel->tasks.empty() && !channel->scheduled;

            if (channel->tasks.size() >= channel->max_queue_size_)
                channel->tasks.pop_front();

            channel->tasks.emplace_back(std::move(task));

            if (need_schedule)
                channel->scheduled = true;
        }

        if (need_schedule)
            executor_.schedule(channel);
    }

    void printGraph() {
        std::cout << "\n===== Node Graph =====\n";

        for (auto& [topic, info] : graph_) {
            for (auto& sub : info.subscribers) {
                std::cout << topic << " --> " << sub << "\n";
            }
        }

        std::cout << "======================\n";
    }

   private:
    // 强语义：这个channel必须存在,类似map的[]
    template <typename T>
    TopicChannel& get_or_create_channel(const std::string& topic, std::type_index type) {
        std::lock_guard<std::mutex> lock(topics_mutex_);
        auto it = topics_.find(topic);

        if (it == topics_.end()) {
            auto channel = std::make_unique<TopicChannel>();
            channel->type = type;

            topics_[topic] = std::move(channel);
        } else {
            if (it->second->type != type)
                throw std::runtime_error("Topic type mismatch");
        }
        return *topics_[topic];
    }

    // 弱语义：这个channel不一定存在，可能没有这个订阅,类似map的find
    template <typename T>
    TopicChannel* get_channel(const std::string& topic) {
        std::lock_guard<std::mutex> lock(topics_mutex_);

        auto it = topics_.find(topic);
        return it == topics_.end() ? nullptr : it->second.get();
    }

   private:
    std::unordered_map<std::string, std::unique_ptr<TopicChannel>> topics_;
    std::mutex topics_mutex_;

    Executor& executor_;

    std::unordered_map<std::string, TopicGraphInfo> graph_;
};