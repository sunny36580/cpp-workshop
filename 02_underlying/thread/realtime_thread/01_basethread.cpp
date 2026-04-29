#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <thread>

// 设置线程为硬实时、绑定CPU核心
void set_realtime(int cpu_core = 3)
{
    // 1. 设置调度策略 & 优先级
    sched_param sp;
    sp.sched_priority = 99;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    // 2. 绑定CPU亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_core, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

void test_thread()
{
    set_realtime();
    while (true)
    {
        // 简单占位
    }
}

int main()
{
    std::thread t(test_thread);
    t.join();
    return 0;
}