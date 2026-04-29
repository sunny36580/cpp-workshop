#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

// 机器人关节指令（上层给底层的输入数据）
struct JointCmd {
    float target_pos = 0.0f;  // 目标角度
    float target_vel = 0.0f;  // 目标速度
};

// 无锁双缓冲模板（量产级极简实现）
// 原理：2份数据，一写一读，原子切换索引，全程无mutex
class LockFreeDoubleBuffer {
private:
    JointCmd buf_[2];   // 两个缓冲区
    std::atomic<int> write_idx_{0};  // 写端索引
    std::atomic<int> read_idx_{1};   // 读端索引

public:
    // 上层线程：写入最新数据（只写）
    void write(const JointCmd& cmd) {
        int w_idx = write_idx_;
        buf_[w_idx] = cmd;
        // 原子切换读写索引（核心无锁操作）
        write_idx_.store(read_idx_.load());

        read_idx_.store(w_idx);

    }

    // 底层实时线程：读取最新数据（只读，不阻塞）
    JointCmd read() const {
        return buf_[read_idx_];
    }
};

// ===================== 测试线程 =====================
LockFreeDoubleBuffer g_cmd_buf;

// 上层线程：100Hz 模拟平衡算法，写入指令
void upper_thread() {
    JointCmd cmd;
    float pos = 0.0f;
    while (true) {
        // 模拟算法计算角度
        cmd.target_pos = pos;
        cmd.target_vel = 0.1f;
        pos += 0.1f;

        // 无锁写入
        g_cmd_buf.write(cmd);

        // 100Hz 循环
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// 底层线程：1000Hz 模拟控制环，无锁读取指令
void control_thread() {
    while (true) {
        // 无锁读取上层算法输入
        JointCmd cmd = g_cmd_buf.read();

        // 模拟底层PD控制（打印验证）
        printf("[控制环] 目标位置: %.2f\n", cmd.target_pos);

        // 1kHz 循环
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// ===================== 主函数 =====================
int main() {
    std::thread t1(upper_thread);
    std::thread t2(control_thread);

    t1.join();
    t2.join();
    return 0;
}