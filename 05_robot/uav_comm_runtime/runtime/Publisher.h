#ifndef PUBLISHER_H
#define PUBLISHER_H
#include "core/ITransport.h"
#include "core/QoS.h"
#include "core/AckManager.h"
#include "core/Message.h"
#include "core/TopicGraph.h"
#include <memory>
#include <string>
#include <atomic>
#include <cstring>

namespace uav_comm {

class Publisher {
public:
    Publisher(std::shared_ptr<ITransport> t, const std::string& topic, const QoS& qos,
              const std::string& node_id, std::shared_ptr<TopicGraph> graph)
        : transport_(t), topic_(topic), qos_(qos), node_id_(node_id), graph_(graph) {
        writer_id_ = generate_writer_id(node_id_, topic_);

        if (qos_.reliability == Reliability::RELIABLE) {
            // 订阅NACK：解析RTPS Bitmap格式
            std::string nack_topic = topic + "/nack";
            transport_->subscribe(nack_topic, [this](Message::ptr msg) {
                handle_nack_bitmap(msg);
            });
        }
    }

    void publish(Message::ptr msg);
    void publish(const std::vector<uint8_t>& payload);
    void publish(std::vector<uint8_t>&& payload);
    void send_heartbeat();

    const std::string& topic() const { return topic_; }

    static uint64_t generate_writer_id(const std::string& node_id, const std::string& topic) {
        uint64_t hash = 0;
        for (char c : node_id) hash = hash * 31 + c;
        for (char c : topic) hash = hash * 31 + c;
        return hash;
    }
private:
    // RTPS标准：解析Bitmap NACK
    void handle_nack_bitmap(Message::ptr msg);

    std::shared_ptr<ITransport> transport_;
    std::string topic_;
    std::shared_ptr<TopicGraph> graph_;
    QoS qos_;
    std::string node_id_; // 节点ID（NACK过滤）
    std::atomic<uint64_t> seq_{0};
    AckManager ack_mgr_;  // 修复：每个Publisher独立缓存
    uint64_t writer_id_;  // 全局唯一发布端ID

};

}
#endif