#include "Publisher.h"
#include <cstring>

namespace uav_comm {

void Publisher::publish(Message::ptr msg) {
    if (!msg || !graph_) return;
    if (!graph_->has_match(topic_, qos_, true)) return;

    if (qos_.reliability == Reliability::BEST_EFFORT) {
        transport_->publish(topic_, msg);
        return;
    }

    // 带序列号封装（RTPS数据帧）
    uint64_t current_seq = seq_++;
    std::vector<uint8_t> data(sizeof(current_seq) + msg->size());
    memcpy(data.data(), &current_seq, sizeof(current_seq));
    memcpy(data.data() + sizeof(current_seq), msg->data(), msg->size());

    auto full_msg = Message::create(std::move(data));
    ack_mgr_.add(writer_id_, current_seq, full_msg->buffer);
    transport_->publish(topic_, full_msg);
}

void Publisher::publish(const std::vector<uint8_t>& payload) {
    publish(Message::create(payload));
}

void Publisher::publish(std::vector<uint8_t>&& payload) {
    publish(Message::create(std::move(payload)));
}

void Publisher::send_heartbeat() {
    if (qos_.reliability != Reliability::RELIABLE) return;
    uint64_t max_seq = ack_mgr_.get_max_seq();
    std::vector<uint8_t> data(sizeof(max_seq));
    memcpy(data.data(), &max_seq, sizeof(max_seq));
    transport_->publish(topic_ + "/heartbeat", Message::create(std::move(data)));
}

// === RTPS 核心：处理Bitmap NACK（一包重传多个丢包） ===
void Publisher::handle_nack_bitmap(Message::ptr msg) {
    if (!msg || msg->size() < 1 + 2*sizeof(uint64_t)) return;

    // 解析节点ID（过滤：只处理发给自己的NACK）
    uint8_t node_id_len = msg->data()[0];
    std::string target_node(msg->data()+1, msg->data()+1+node_id_len);
    if (target_node != node_id_) return;

    size_t offset = 1 + node_id_len;
    uint64_t base_seq = 0;
    uint64_t nack_bitmap = 0;

    memcpy(&base_seq, msg->data() + offset, sizeof(base_seq));
    offset += sizeof(base_seq);
    memcpy(&nack_bitmap, msg->data() + offset, sizeof(nack_bitmap));

    // 批量重传（RTPS标准）
    for (int i = 0; i < 63; ++i) {
        if (nack_bitmap & (1ULL << i)) {
            uint64_t lost_seq = base_seq + i;
            auto data = ack_mgr_.get_resend(writer_id_, lost_seq, lost_seq);
            if (!data.empty()) {
                transport_->publish(topic_, Message::create(data[0]));
            }
        }
    }
}

} // namespace uav_comm