#pragma once

#include <cstdio>
#include <string>
#include <ctime>

namespace robot_runtime {

// 简化的日志工具，后续可替换为 spdlog
inline void log_info(const std::string& tag, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&tt));
    fprintf(stdout, "[%s] [%s] %s\n", buf, tag.c_str(), msg.c_str());
}

inline void log_error(const std::string& tag, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&tt));
    fprintf(stderr, "[%s] [%s] ERROR: %s\n", buf, tag.c_str(), msg.c_str());
}

} // namespace robot_runtime
