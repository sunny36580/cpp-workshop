#ifndef SUBSCRIPTION_H
#define SUBSCRIPTION_H
#include "core/QoS.h"
#include "core/Message.h"
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>
#include <memory>

namespace uav_comm {

class ITransport;
class Node;

class Subscription {
public:
    using Callback = std::function<void(const std::vector<uint8_t>&)>;

    Subscription(const std::string& topic, const QoS& qos, Callback cb, Node& node);
    void on_raw(Message::ptr msg, std::shared_ptr<ITransport> transport);
    // 心跳处理（RTPS标准：推进窗口+检测丢包）
    void on_heartbeat(uint64_t publisher_max_seq, std::shared_ptr<ITransport> transport);

    const std::string& topic() const { return topic_; }
    const QoS& qos() const { return qos_; }

private:
    // RTPS标准：Bitmap丢包检测
    void check_loss(std::shared_ptr<ITransport> transport);
    // RTPS标准：发送Bitmap NACK
    void send_nack_bitmap(uint64_t base_seq, uint64_t bitmap, std::shared_ptr<ITransport> transport);

    std::string topic_;
    QoS qos_;
    Callback cb_;
    Node& node_;
    
    // === RTPS 可靠传输状态 ===
    uint64_t last_seq_ = 0;            // 本地最大接收序列号
    uint64_t bitmap_ = 0;               // 64位丢包位图
    uint64_t last_nack_base_ = 0;      // NACK防风暴（基准值）
    uint64_t publisher_last_seq_ = 0;   // 发布端最新序列号（心跳同步）

    bool initialized_ = false; // 初始化标志
    bool first_packet_ = true; //
};

} // namespace uav_comm
#endif // SUBSCRIPTION_H