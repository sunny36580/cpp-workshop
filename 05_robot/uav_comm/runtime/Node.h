#ifndef NODE_H
#define NODE_H

#include "core/ITransport.h"
#include "core/TopicGraph.h"
#include "core/Reactor.h" 
#include "Publisher.h"
#include "Subscription.h"
#include <memory>
#include <unordered_set>
#include <mutex>
#include <vector>
#include <string>

namespace uav_comm {

class Node {
public:
    Node(std::shared_ptr<ITransport> t, const std::string& node_id, int domain_id = 0);
    ~Node();

    std::shared_ptr<Publisher> create_publisher(const std::string& topic, const QoS& qos = {});
    using MessageCallback = std::function<void(const std::vector<uint8_t>&)>;
    std::shared_ptr<Subscription> create_subscription(const std::string& topic, const QoS& qos, MessageCallback cb);

    int get_domain_id() const;
    void set_domain_id(int domain_id);
    std::string node_id() const;

private:
    std::string domain_topic(const std::string& name) const;
    void broadcast(const TopicInfo& info);
    void broadcast_all();
    void handle_discovery(const std::vector<uint8_t>& data);
    void send_heartbeat();
    void publish_all_heartbeats(); //发送所有Publisher心跳

    std::shared_ptr<ITransport> transport_;
    std::string node_id_;
    int domain_id_;

    std::shared_ptr<TopicGraph> graph_;
    std::unordered_set<std::string> subscribed_;
    std::vector<std::shared_ptr<Publisher>> publishers_; // 管理所有发布者
    std::mutex sub_mtx_;
    std::mutex pub_mtx_;

    std::string disc_topic_;
    std::string hb_topic_;
    
};

} // namespace uav_comm

#endif