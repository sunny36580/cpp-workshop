#pragma once
#include <functional>

// 传感器抽象基类（模板）
template<typename T>
class Sensor {
public:
    using DataCallback = std::function<void(const T&)>;
    virtual ~Sensor() = default;
    virtual void start(DataCallback cb) = 0;
};