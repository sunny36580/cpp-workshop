#pragma once
#include <atomic>
#include <memory>
#include <utility>

/**
 * @brief 通用无锁懒加载对象池（生产级、零业务耦合）
 * @tparam T 任意可默认构造的对象类型（无侵入、无耦合）
 */
template <typename T>
class SafeLazyPool {
private:
    // ======================
    // 修复1：缓存行对齐，无手动padding（杜绝UB）
    // ======================
    struct alignas(64) Node {
        T obj;
        Node* next = nullptr;
    };

    // ======================
    // 修复2：head_ 单独缓存行对齐（核心伪共享优化）
    // ======================
    alignas(64) std::atomic<Node*> head_{nullptr};
    std::atomic<bool> destroyed_{false};
    const size_t max_size_;
    std::atomic<size_t> pool_size_{0};

    // ======================
    // 修复3：线程本地无锁随机数（替代全局锁rand()）
    // ======================
    thread_local static uint32_t rand_seed_;
    static uint32_t fast_rand() {
        rand_seed_ = rand_seed_ * 1103515245 + 12345;
        return rand_seed_;
    }

    // ======================
    // 修复4：严格并发安全的push，内存上限永不突破
    // ======================
    void push(Node* node) noexcept {
        // CAS 抢占名额，严格控制池大小，多线程下不超上限
        size_t expected = pool_size_.load(std::memory_order_relaxed);
        while (expected < max_size_) {
            if (pool_size_.compare_exchange_weak(
                expected, expected + 1,
                std::memory_order_relaxed, std::memory_order_relaxed
            )) {
                // 名额抢占成功，入池
                Node* old_head = head_.load(std::memory_order_relaxed);
                do {
                    node->next = old_head;
                } while (!head_.compare_exchange_weak(
                    old_head, node,
                    std::memory_order_release, std::memory_order_relaxed
                ));
                return;
            }
        }

        // 超出上限：直接销毁节点，不回收
        delete node;
    }

    // 无锁出栈
    Node* pop() noexcept {
        Node* old_head = head_.load(std::memory_order_acquire);
        while (old_head) {
            Node* next = old_head->next;
            if (head_.compare_exchange_weak(
                old_head, next,
                std::memory_order_acquire, std::memory_order_relaxed
            )) {
                pool_size_.fetch_sub(1, std::memory_order_relaxed);
                return old_head;
            }
        }
        return nullptr;
    }

    // ======================
    // 通用安全重置（零业务耦合！）
    // 仅做基础清空，不触碰业务逻辑
    // ======================
    static void generic_reset(T& obj) {
        // 通用安全重置：不修改业务成员，仅重置为默认状态（无性能损耗）
        // 如需业务特化，由用户外部实现，池不耦合
        obj = T{};
    }

public:
    /**
     * @brief 构造函数
     * @param max_cache_size 池最大缓存对象数（控制内存上限）
     */
    explicit SafeLazyPool(size_t max_cache_size) 
        : max_size_(max_cache_size) {}

    ~SafeLazyPool() {
        destroyed_.store(true, std::memory_order_release);
        while (Node* node = pop()) delete node;
    }

    // ======================
    // 唯一对外接口：获取shared_ptr，全自动生命周期
    // ======================
    std::shared_ptr<T> get() {
        // 1. 从池复用
        if (Node* node = pop()) {
            return bind_deleter(node);
        }
        // 2. 懒加载新建
        Node* node = new Node{};
        return bind_deleter(node);
    }

    // 禁用拷贝/移动，杜绝线程安全问题
    SafeLazyPool(const SafeLazyPool&) = delete;
    SafeLazyPool& operator=(const SafeLazyPool&) = delete;
    SafeLazyPool(SafeLazyPool&&) = delete;
    SafeLazyPool& operator=(SafeLazyPool&&) = delete;

private:
    // 绑定智能指针删除器
    std::shared_ptr<T> bind_deleter(Node* node) {
        return std::shared_ptr<T>(
            &node->obj,
            [this, node](T*) {
                if (destroyed_.load(std::memory_order_acquire)) {
                    delete node;
                    return;
                }
                // 通用重置，无业务耦合
                generic_reset(node->obj);
                push(node);
            }
        );
    }
};

// 初始化线程本地随机数种子
template <typename T>
thread_local uint32_t SafeLazyPool<T>::rand_seed_ = 1234567;