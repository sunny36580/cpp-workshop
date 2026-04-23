#pragma once
#include <unordered_map>
#include <string>
#include "../utils/Time.h"

// 传感器存活监控（超时判定掉线）
class SensorMonitor {
public:
    // 更新传感器最后活跃时间
    void update(const std::string& name) {
        last_ts_[name] = now_ms();
    }

    // 检测是否存活
    bool is_alive(const std::string& name, int timeout_ms) {
        // 未初始化 → 判定离线
        if (last_ts_.find(name) == last_ts_.end()) return false;
        return now_ms() - last_ts_[name] < timeout_ms;
    }

private:
    std::unordered_map<std::string, int64_t> last_ts_;
};