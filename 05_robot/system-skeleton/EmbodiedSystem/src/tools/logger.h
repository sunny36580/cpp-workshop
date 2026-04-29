// src/tools/logger.h
#pragma once
#include <iostream>
#include <chrono>

#define LOG_INFO(msg) \
    std::cout << "[INFO][" << __FILE__ << ":" << __LINE__ << "] " << msg << std::endl;

#define LOG_WARN(msg) \
    std::cout << "[WARN][" << __FILE__ << ":" << __LINE__ << "] " << msg << std::endl;

// 数据流追踪：知道谁发给谁、什么时间
struct DataTrace {
    std::string from;
    std::string to;
    std::string topic;
    double ts;
};