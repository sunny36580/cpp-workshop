#pragma once

#include <cstdio>
#include <string>
#include <ctime>
#include <chrono>

namespace robot_runtime {

// ============================================================================
// Log — 极简日志工具
// ============================================================================
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

inline const char* level_str(LogLevel lv) {
    switch (lv) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
    }
    return "UNKNOWN";
}

inline void log(LogLevel level, const std::string& tag, const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()) % 1000;
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&tt));
    fprintf(stdout, "[%s.%03ld] [%s] [%s] %s\n",
            buf, (long)ms.count(), level_str(level), tag.c_str(), msg.c_str());
}

inline void log_info(const std::string& tag, const std::string& msg) {
    log(LogLevel::INFO, tag, msg);
}

inline void log_warn(const std::string& tag, const std::string& msg) {
    log(LogLevel::WARN, tag, msg);
}

inline void log_error(const std::string& tag, const std::string& msg) {
    log(LogLevel::ERROR, tag, msg);
}

} // namespace robot_runtime
