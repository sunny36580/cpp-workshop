#ifndef LOCK_FREE_QUEUE_H
#define LOCK_FREE_QUEUE_H

#include <atomic>
#include <functional>
#include <memory>

namespace uav_comm {

using Task = std::function<void()>;

// 无锁队列（CAS 实现，高性能、线程安全）
template<typename T>
class LockFreeQueue {
public:
    LockFreeQueue() {
        Node* dummy = new Node();
        head_.store(dummy);
        tail_.store(dummy);
    }

    ~LockFreeQueue() {
        while (pop());
        delete head_.load();
    }

    // 无锁入队
    void push(T&& val) {
        Node* new_node = new Node(std::move(val));
        Node* old_tail = nullptr;

        while (true) {
            old_tail = tail_.load();
            Node* next = old_tail->next.load();
            if (old_tail == tail_.load()) {
                if (next == nullptr) {
                    if (old_tail->next.compare_exchange_weak(next, new_node)) {
                        break;
                    }
                } else {
                    tail_.compare_exchange_weak(old_tail, next);
                }
            }
        }
        tail_.compare_exchange_weak(old_tail, new_node);
    }

    // 无锁出队
    bool pop(T* out = nullptr) {
        Node* old_head = nullptr;
        while (true) {
            old_head = head_.load();
            Node* old_tail = tail_.load();
            Node* next = old_head->next.load();

            if (old_head == head_.load()) {
                if (old_head == old_tail) {
                    if (next == nullptr) return false;
                    tail_.compare_exchange_weak(old_tail, next);
                } else {
                    if (out && next) *out = std::move(next->data);
                    head_.store(next);
                    delete old_head;
                    return true;
                }
            }
        }
    }

private:
    struct Node {
        T data;
        std::atomic<Node*> next{nullptr};
        Node() = default;
        Node(T&& d) : data(std::move(d)) {}
    };

    std::atomic<Node*> head_{nullptr};
    std::atomic<Node*> tail_{nullptr};
};

} // namespace uav_comm

#endif