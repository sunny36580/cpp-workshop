#ifndef UAV_COMM_H
#define UAV_COMM_H

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "core/QoS.h"

namespace uav_comm {

using MessageCallback = std::function<void(const std::vector<uint8_t>&)>;

// 内部类前置声明
class ITransport;
class Node;
class Publisher;
class Subscription;

// 对外 Transport
class UavTransport {
public:
    static std::shared_ptr<UavTransport> create();
    ~UavTransport();

    // 给一个公开接口取出内部 ITransport
    std::shared_ptr<ITransport> get_internal() { return impl_; }

private:
    std::shared_ptr<ITransport> impl_;
};

class UavPublisher {
public:
    void publish(const std::vector<uint8_t>& data);
    void publish(std::vector<uint8_t>&& data);
    ~UavPublisher();
private:
    std::shared_ptr<Publisher> impl_;
    friend class UavNode;
};

// 对外 Subscription
class UavSubscription {
private:
    std::shared_ptr<Subscription> impl_;
    friend class UavNode;
};

// 对外 Node
class UavNode {
public:
    explicit UavNode(std::shared_ptr<UavTransport> transport,
            const std::string& node_id,
            int domain_id = 0);
    ~UavNode();

    std::shared_ptr<UavPublisher> create_publisher(
        const std::string& topic,
        const QoS& qos = {}
    );

    std::shared_ptr<UavSubscription> create_subscription(
        const std::string& topic,
        MessageCallback cb,
        const QoS& qos = {}
    );

private:
    std::shared_ptr<Node> impl_;
};

} // namespace

#endif