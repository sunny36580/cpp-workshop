#pragma once
#include <functional>
#include <memory>
#include "../core/Frame.h"

// 传感器抽象基类（模板）
template<typename T>
class Sensor {
public:
    using Ptr = std::shared_ptr<T>;
    using Callback = std::function<void(Ptr)>;
    virtual ~Sensor() = default;
    virtual void start(Callback cb) = 0;
};