#include <iostream>
#include <ctime>
#include <chrono>
#include <thread>

// 你写的 高精度绝对时间睡眠（硬实时专用）
void precise_sleep_ms(int ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    ts.tv_nsec += ms * 1000000;
    if (ts.tv_nsec >= 1000000000)
    {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000;
    }
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
}

// 测试：普通系统sleep（对比用）
void normal_sleep_ms(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main()
{
    // 记录上一次时间
    auto last = std::chrono::high_resolution_clock::now();

    while (true)
    {
        // 计算当前周期耗时（单位：毫秒）
        auto now = std::chrono::high_resolution_clock::now();
        double cost = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;

        // 打印周期：越接近1.0ms越好
        printf("精准睡眠周期: %.2f ms\n", cost);

        // ============== 测试切换 ==============
        precise_sleep_ms(1);   // 你的精准睡眠（低延迟）
        // normal_sleep_ms(1);  // 普通睡眠（高抖动）
    }
    return 0;
}