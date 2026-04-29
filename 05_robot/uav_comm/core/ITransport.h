#ifndef ITRANSPORT_H
#define ITRANSPORT_H
#include <functional>
#include <string>
#include <vector>
#include "Message.h"

namespace uav_comm {

// 统一回调类型（避免到处写模板）
using RawCallback = std::function<void(Message::ptr msg)>;

class ITransport {
public:
    virtual ~ITransport() = default;

    // 发布数据
    virtual void publish(const std::string& topic, Message::ptr msg) = 0;

    // 订阅数据
    virtual void subscribe(const std::string& topic, RawCallback cb) = 0;

    // 取消订阅数据
    virtual void unsubscribe(const std::string& topic) = 0;
};

}  // namespace uav_comm

#endif  // ITRANSPORT_H