#include "Subscription.h"
#include "Node.h"
#include <cstring>

namespace uav_comm {

Subscription::Subscription(const std::string& topic, const QoS& qos, Callback cb, Node& node)
    : topic_(topic), qos_(qos), cb_(cb), node_(node),
      last_seq_(0), bitmap_(0), last_nack_base_(0), publisher_last_seq_(0), initialized_(false) {}

void Subscription::on_raw(Message::ptr msg, std::shared_ptr<ITransport> transport) {
    if (!msg) return;

    // 尽力模式：直接回调
    if (qos_.reliability == Reliability::BEST_EFFORT) {
        if (cb_) cb_(msg->buffer);
        return;
    }
    
    if (msg->size() < sizeof(uint64_t)) return;

    // 解析序列号
    uint64_t seq;
    memcpy(&seq, msg->data(), sizeof(uint64_t));

    // 强制收第一个包（seq=0 必过）
    if (first_packet_) {
        first_packet_ = false;
        last_seq_ = seq;
        bitmap_ = 1ULL;
        std::vector<uint8_t> payload(msg->data() + sizeof(uint64_t), msg->data() + msg->size());
        if (cb_) cb_(payload);
        check_loss(transport);
        return;
    }

    // 去重逻辑
    if (seq <= last_seq_) {
        uint64_t shift = last_seq_ - seq;
        if (shift < 64 && (bitmap_ & (1ULL << shift))) {
            return;
        }
    }

    // 更新滑动窗口
    if (seq > last_seq_) {
        uint64_t shift = seq - last_seq_;
        bitmap_ = (shift < 64) ? (bitmap_ << shift) : 0;
        bitmap_ |= 1ULL;
        last_seq_ = seq;
    }

    // 检测丢包
    check_loss(transport);

    // 业务回调
    std::vector<uint8_t> payload(msg->data() + sizeof(uint64_t), msg->data() + msg->size());
    if (cb_) cb_(payload);
}

// ====================== 删除错误判断，启用NACK防风暴 ======================
void Subscription::check_loss(std::shared_ptr<ITransport> transport) {
    if (qos_.reliability != Reliability::RELIABLE) return;

    uint64_t base_seq = last_seq_ >= 63 ? (last_seq_ - 63) : 0;
    uint64_t nack_bitmap = 0;
    bool has_loss = false;

    for (int i = 1; i < 64; ++i) {
        if (!(bitmap_ & (1ULL << i))) {
            nack_bitmap |= (1ULL << (i - 1));
            has_loss = true;
        }
    }

    // 防NACK风暴：同一基准值只发一次
    if (has_loss && base_seq != last_nack_base_) {
        last_nack_base_ = base_seq;
        send_nack_bitmap(base_seq, nack_bitmap, transport);
    }
}

// ====================== 心跳逻辑（不变，已修复） ======================
void Subscription::on_heartbeat(uint64_t publisher_max_seq, std::shared_ptr<ITransport> transport) {
    if (qos_.reliability != Reliability::RELIABLE) return;
    if (publisher_max_seq <= last_seq_) return;

    // 分段推进窗口
    while (publisher_max_seq - last_seq_ >= 64) {
        bitmap_ = 0;
        last_seq_ += 64;
        check_loss(transport);
    }

    uint64_t shift = publisher_max_seq - last_seq_;
    if (shift > 0) {
        bitmap_ <<= shift;
        last_seq_ = publisher_max_seq;
    }

    check_loss(transport);
}

// ====================== NACK发送（不变） ======================
void Subscription::send_nack_bitmap(uint64_t base_seq, uint64_t bitmap, std::shared_ptr<ITransport> transport) {
    std::vector<uint8_t> data;
    const std::string& node_id = node_.node_id();

    data.push_back(static_cast<uint8_t>(node_id.size()));
    data.insert(data.end(), node_id.begin(), node_id.end());

    const uint8_t* base_bytes = reinterpret_cast<const uint8_t*>(&base_seq);
    data.insert(data.end(), base_bytes, base_bytes + sizeof(base_seq));

    const uint8_t* mask_bytes = reinterpret_cast<const uint8_t*>(&bitmap);
    data.insert(data.end(), mask_bytes, mask_bytes + sizeof(bitmap));

    transport->publish(topic_ + "/nack", Message::create(std::move(data)));
}

} // namespace uav_comm