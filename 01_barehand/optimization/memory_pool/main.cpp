#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include "frame.h"
#include "pool.h"

using Clock = std::chrono::high_resolution_clock;

long long now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

// ==========================
// 实验1：new/delete
// ==========================
void test_new_delete(size_t N) {
    auto start = now_ms();

    for (size_t i = 0; i < N; ++i) {
        Frame* f = new Frame();
        // 模拟访问，防止被优化
        f->data[0] = static_cast<char>(i);
        delete f;
    }

    auto end = now_ms();
    std::cout << "[new/delete] time = " << (end - start) << " ms\n";
}

// ==========================
// 实验2：对象池（单线程）
// ==========================
void test_pool_single(size_t N) {
    ObjectPool<Frame> pool(1024);

    auto start = now_ms();

    for (size_t i = 0; i < N; ++i) {
        Frame* f = pool.acquire();
        f->data[0] = static_cast<char>(i);
        pool.release(f);
    }

    auto end = now_ms();
    std::cout << "[pool single] time = " << (end - start) << " ms\n";
}

// ==========================
// 实验3：对象池（多线程）
// ==========================
void test_pool_multi(size_t N, int threads) {
    ObjectPool<Frame> pool(1024);

    auto start = now_ms();

    std::vector<std::thread> ts;
    size_t per_thread = N / threads;

    for (int t = 0; t < threads; ++t) {
        ts.emplace_back([&]() {
            for (size_t i = 0; i < per_thread; ++i) {
                Frame* f = pool.acquire();
                f->data[0] = static_cast<char>(i);
                pool.release(f);
            }
        });
    }

    for (auto& t : ts) t.join();

    auto end = now_ms();
    std::cout << "[pool multi] time = " << (end - start) << " ms\n";
}

int main() {
    size_t N = 200000;   // 可调（先别太大，2MB * N 会很猛）
    int threads = 4;

    test_new_delete(N);
    test_pool_single(N);
    test_pool_multi(N, threads);

    return 0;
}