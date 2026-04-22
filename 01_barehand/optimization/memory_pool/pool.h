#pragma once
#include <vector>
#include <mutex>
#include <memory>

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t size) {
        pool_.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            pool_.push_back(new T());
        }
    }

    ~ObjectPool() {
        for (auto p : pool_) delete p;
    }

    T* acquire() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (pool_.empty()) {
            // fallback（避免阻塞）：直接 new（可统计次数）
            return new T();
        }
        T* obj = pool_.back();
        pool_.pop_back();
        return obj;
    }

    void release(T* obj) {
        std::lock_guard<std::mutex> lock(mtx_);
        pool_.push_back(obj);
    }

private:
    std::vector<T*> pool_;
    std::mutex mtx_;
};