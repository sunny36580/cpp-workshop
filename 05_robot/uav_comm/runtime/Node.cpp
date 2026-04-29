#include "Node.h"
#include "core/Message.h"
#include "core/Reactor.h"
#include <cstring>

namespace uav_comm {

Node::Node(std::shared_ptr<ITransport> t, const std::string& node_id, int domain_id)
    : transport_(t),
      node_id_(node_id),
      domain_id_(domain_id),
      graph_(std::make_shared<TopicGraph>())
{
    disc_topic_ = domain_topic("discovery");
    hb_topic_ = domain_topic("__heartbeat");

    // 发现订阅
    transport_->subscribe(disc_topic_, [this](Message::ptr msg) {
        if (!msg) return;
        const auto& d = msg->buffer;
        Reactor::instance().post([this, d](){ handle_discovery(d); });
    });

    // 心跳订阅
    transport_->subscribe(hb_topic_, [this](Message::ptr msg) {
        if (!msg) return;
        const auto& d = msg->buffer;
        Reactor::instance().post([this, d](){
            graph_->update_node_alive({d.begin(), d.end()});
        });
    });


    // 反应堆启动 + 定时任务
    Reactor::instance().start();
    Reactor::instance().set_interval(1000,  [this]() { send_heartbeat(); });
    Reactor::instance().set_interval(3000,  [this]() { broadcast_all(); });
    Reactor::instance().set_interval(2000,  [this]() { graph_->cleanup_node(5000); });
    Reactor::instance().set_interval(1000,  [this]() { publish_all_heartbeats(); }); // 每秒发Publisher心跳
}

Node::~Node() {
    Reactor::instance().stop();
}

std::shared_ptr<Publisher> Node::create_publisher(
    const std::string& topic, const QoS& qos)
{
    std::string full_topic = domain_topic(topic);
    TopicInfo info;
    info.node_id = node_id_;
    info.topic = full_topic;
    info.qos = qos;
    info.is_publisher = true;
    graph_->add(info);
    broadcast(info);

    // 传入 AckManager（仅用于消息缓存）
    auto pub = std::make_shared<Publisher>(transport_, full_topic, qos, node_id_, graph_);
    
    std::lock_guard<std::mutex> lock(pub_mtx_);
    publishers_.push_back(pub); // 管理发布者
    return pub;
}

std::shared_ptr<Subscription> Node::create_subscription(
    const std::string& topic, const QoS& qos, MessageCallback cb)
{
    std::string full_topic = domain_topic(topic);
    auto sub = std::make_shared<Subscription>(full_topic, qos, cb, *this);

    TopicInfo info;
    info.node_id = node_id_;
    info.topic = full_topic;
    info.qos = qos;
    info.is_publisher = false;
    graph_->add(info);
    broadcast(info);

    std::lock_guard<std::mutex> lock(sub_mtx_);
    if (!subscribed_.count(full_topic)) {
        // ====================== 仅订阅【数据通道】，纯业务数据 ======================
        transport_->subscribe(full_topic, [this, sub](Message::ptr msg) {
            Reactor::instance().post([sub, msg, this]() {
                sub->on_raw(msg, transport_);
            });
        });
        subscribed_.insert(full_topic);

        // ====================== 单独订阅【心跳控制通道】，严格隔离 ======================
        if (qos.reliability == Reliability::RELIABLE) {
            std::string hb_topic = full_topic + "/heartbeat";
            transport_->subscribe(hb_topic, [this, sub](Message::ptr msg) {
                Reactor::instance().post([sub, msg, this]() {
                    if (msg->size() < sizeof(uint64_t)) return;
                    uint64_t max_seq;
                    memcpy(&max_seq, msg->data(), sizeof(max_seq));
                    sub->on_heartbeat(max_seq, transport_);
                });
            });
        }
    }
    return sub;
}

// 发送所有Publisher心跳,
void Node::publish_all_heartbeats() {
    std::lock_guard<std::mutex> lock(pub_mtx_);
    for (auto& pub : publishers_) {
        pub->send_heartbeat();
    }
}

std::string Node::node_id() const { return node_id_; }

int Node::get_domain_id() const { return domain_id_; }

void Node::set_domain_id(int domain_id) {
    std::lock_guard<std::mutex> lock(sub_mtx_);
    if (domain_id_ == domain_id) return;

    transport_->unsubscribe(disc_topic_);
    transport_->unsubscribe(hb_topic_);
    domain_id_ = domain_id;
    disc_topic_ = domain_topic("discovery");
    hb_topic_ = domain_topic("__heartbeat");
    graph_->clear();

    transport_->subscribe(disc_topic_, [this](Message::ptr msg){
        if (!msg) return;
        handle_discovery(msg->buffer);
    });
    transport_->subscribe(hb_topic_, [this](Message::ptr msg){
        if (!msg) return;
        graph_->update_node_alive({msg->buffer.begin(), msg->buffer.end()});
    });
    broadcast_all();
}


std::string Node::domain_topic(const std::string& name) const {
    return "domain/" + std::to_string(domain_id_) + "/" + name;
}

void Node::broadcast(const TopicInfo& info) {
    std::vector<uint8_t> data;
    auto write_tlv = [&](uint8_t type, const std::string& val) {
        data.push_back(type);
        uint16_t len = val.size();
        const uint8_t* len_bytes = reinterpret_cast<const uint8_t*>(&len);
        data.insert(data.end(), len_bytes, len_bytes + 2);  
        data.insert(data.end(), val.begin(), val.end());
    };
    write_tlv(1, info.node_id);
    write_tlv(2, info.topic);
    write_tlv(3, info.is_publisher ? "pub" : "sub");
    write_tlv(4, info.qos.reliability == Reliability::RELIABLE ? "R" : "BE");
    transport_->publish(disc_topic_, Message::create(std::move(data)));
}

void Node::broadcast_all() {
    std::lock_guard<std::mutex> lock(sub_mtx_);
    for (auto& info : graph_->match("")) {
        if (info.node_id == node_id_) broadcast(info);
    }
}

void Node::handle_discovery(const std::vector<uint8_t>& data) {
    TopicInfo info;
    size_t i = 0;
    while (i < data.size()) {
        uint8_t type = data[i++];
        uint16_t len; memcpy(&len, &data[i], 2); i+=2;
        std::string val(data.begin()+i, data.begin()+i+len); i+=len;
        switch (type) {
            case 1: info.node_id = val; break;
            case 2: info.topic = val; break;
            case 3: info.is_publisher = (val == "pub"); break;
            case 4: info.qos.reliability = (val == "R") ? Reliability::RELIABLE : Reliability::BEST_EFFORT; break;
        }
    }
    graph_->add(info);
    graph_->update_node_alive(info.node_id);
}

void Node::send_heartbeat() {
    std::vector<uint8_t> data(node_id_.begin(), node_id_.end());
    transport_->publish(hb_topic_, Message::create(std::move(data)));
}

} // namespace uav_comm