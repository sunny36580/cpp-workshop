#include <iostream>
#include <thread>       // C++多线程
#include <atomic>       // 原子变量（线程安全，无锁）
#include <vector>
#include <chrono>       // 高精度计时
#include <cstring>
#include <pthread.h>    // Linux系统调用：CPU亲和性、线程绑核

// ==========================
// 功能宏开关（面试核心实验变量）
// 0=关闭 1=开启，组合做4组实验
// ==========================
// 开启：缓存行对齐，解决伪共享 | 关闭：触发伪共享
#define USE_PADDING 1
// 开启：CPU绑核 | 关闭：系统自动调度线程
#define USE_AFFINITY 1

// ==========================
// CPU缓存行大小：64字节（x86架构固定）
// 多个线程修改**同一缓存行**的不同变量，会触发缓存颠簸（性能暴跌）
// ==========================
#if USE_PADDING
// alignas(64)：强制按64字节缓存行对齐
// 让两个计数器独占一个缓存行，彻底避免伪共享
struct alignas(64) CounterA {
    std::atomic<long> value{0};  // 原子变量：线程安全自增
};

struct alignas(64) CounterB {
    std::atomic<long> value{0};
};

#else
// 无对齐：两个原子变量挤在**同一个64字节缓存行**中
// 线程t1改a、t2改b → 缓存行频繁失效 → 伪共享（性能巨慢）
struct Counter {
    std::atomic<long> a{0};
    std::atomic<long> b{0};
};
#endif

// ==========================
// 【面试核心】CPU亲和性（绑核）函数
// 作用：把当前线程**固定在指定CPU核心**上
// 优点：避免线程在多核间迁移 → 缓存不失效 → 性能稳定
// ==========================
void bind_cpu(int cpu_id) {
#if USE_AFFINITY
    cpu_set_t set;               // CPU核心集合
    CPU_ZERO(&set);              // 清空集合
    CPU_SET(cpu_id, &set);       // 将指定核心加入集合
    // 系统调用：设置当前线程的CPU亲和性
    pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#endif
}

// ==========================
// 高精度计时工具（微秒级）
// 用于统计程序执行耗时，性能测试必备
// ==========================
long long now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

// ==========================
// 主函数：性能测试主逻辑
// 两个线程并行对原子变量做1亿次自增，测试耗时
// ==========================
int main() {
    const long N = 100000000;    // 循环次数：1亿次（足够触发性能差异）
    long long start = now_us();   // 记录开始时间

#if USE_PADDING
    // 开启对齐：两个独立的计数器，无伪共享
    CounterA a;
    CounterB b;

    // 线程1：绑定CPU0，自增计数器a
    std::thread t1([&]() {
        bind_cpu(0);
        for (long i = 0; i < N; i++) {
            a.value++;
        }
    });

    // 线程2：绑定CPU1，自增计数器b
    std::thread t2([&]() {
        bind_cpu(1);
        for (long i = 0; i < N; i++) {
            b.value++;
        }
    });

#else
    // 关闭对齐：共享同一个Counter结构体，触发伪共享
    Counter c;

    std::thread t1([&]() {
        bind_cpu(0);
        for (long i = 0; i < N; i++) {
            c.a++;  // 修改缓存行前8字节
        }
    });

    std::thread t2([&]() {
        bind_cpu(1);
        for (long i = 0; i < N; i++) {
            c.b++;  // 修改同一个缓存行的后8字节
        }
    });
#endif

    // 等待两个线程执行完毕
    t1.join();
    t2.join();

    // 计算总耗时（转毫秒）
    long long end = now_us();
    std::cout << "Time: " << (end - start) / 1000.0 << " ms\n";

    // 打印当前实验模式
#if USE_PADDING
    std::cout << "[Mode] 缓存行对齐 ON（无伪共享）\n";
#else
    std::cout << "[Mode] 缓存行对齐 OFF（触发伪共享）\n";
#endif

#if USE_AFFINITY
    std::cout << "[Mode] CPU绑核 ON（性能稳定）\n";
#else
    std::cout << "[Mode] CPU绑核 OFF（线程随机调度）\n";
#endif

    return 0;
}