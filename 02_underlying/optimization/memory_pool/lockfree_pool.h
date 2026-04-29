#pragma once
#include <atomic>
#include <vector>
#include <memory>

template<typename T>
class LockFreePool {
private:
    struct Node {
        T* obj;
        Node* next;
    };

    std::atomic<Node*> head_{nullptr};

public:
    explicit LockFreePool(size_t size) {
        // 预分配对象 + 节点
        for (size_t i = 0; i < size; ++i) {
            auto node = new Node{new T(), nullptr};
            push(node);
        }
    }

    ~LockFreePool() {
        Node* node = head_.load();
        while (node) {
            Node* next = node->next;
            delete node->obj;
            delete node;
            node = next;
        }
    }

    // ==========================
    // 获取对象
    // ==========================
    T* acquire() {
        Node* node = pop();
        if (!node) {
            // fallback（极端情况）
            return new T();
        }

        T* obj = node->obj;
        delete node;  // Node本身可以释放（小对象）
        return obj;
    }

    // ==========================
    // 归还对象
    // ==========================
    void release(T* obj) {
        auto node = new Node{obj, nullptr};
        push(node);
    }

    // ==========================
    // 直接给 shared_ptr 用（关键）
    // ==========================
    std::shared_ptr<T> acquire_shared() {
        T* obj = acquire();

        return std::shared_ptr<T>(
            obj,
            [this](T* p) {
                this->release(p);
            }
        );
    }

private:

    // ==========================
    // lock-free push
    // ==========================
    void push(Node* node) {
        Node* old_head = head_.load(std::memory_order_relaxed);

        do {
            node->next = old_head;
        } while (!head_.compare_exchange_weak(
            old_head,
            node,
            std::memory_order_release,
            std::memory_order_relaxed
        ));
    }

    // ==========================
    // lock-free pop
    // ==========================
    Node* pop() {
        Node* old_head = head_.load(std::memory_order_acquire);

        while (old_head) {
            Node* next = old_head->next;

            if (head_.compare_exchange_weak(
                old_head,
                next,
                std::memory_order_acquire,
                std::memory_order_relaxed
            )) {
                return old_head;
            }
        }

        return nullptr;
    }
};