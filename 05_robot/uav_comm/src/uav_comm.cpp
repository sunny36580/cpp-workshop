#include "uav_comm/uav_comm.h"
#include "core/Message.h"
#include "core/ITransport.h"
#include "runtime/Node.h"
#include "runtime/Publisher.h"
#include "runtime/Subscription.h"
#include "transport/zenoh/ZenohTransport.h"

namespace uav_comm {

// UavTransport
std::shared_ptr<UavTransport> UavTransport::create() {
    auto t = std::make_shared<UavTransport>();
    t->impl_ = std::make_shared<ZenohTransport>();
    return t;
}
UavTransport::~UavTransport() = default;

// UavPublisher
// 对外vector → 内部自动转MessagePtr
void UavPublisher::publish(const std::vector<uint8_t>& data) {
    if (!impl_) return;
    impl_->publish(Message::create(data));
}
void UavPublisher::publish(std::vector<uint8_t>&& data) {
    if (!impl_) return;
    // 移动语义=零拷贝
    impl_->publish(Message::create(std::move(data)));
}
UavPublisher::~UavPublisher() = default;

// ------------------------------
// UavNode
// ------------------------------
UavNode::UavNode(std::shared_ptr<UavTransport> t, const std::string& id, int dom)
{
    impl_ = std::make_shared<Node>(t->get_internal(), id, dom);
}
UavNode::~UavNode() = default;

std::shared_ptr<UavPublisher>
UavNode::create_publisher(const std::string& topic, const QoS& qos) {
    auto p = std::make_shared<UavPublisher>();
    p->impl_ = impl_->create_publisher(topic, qos);
    return p;
}

std::shared_ptr<UavSubscription>
UavNode::create_subscription(const std::string& topic, MessageCallback cb, const QoS& qos) {
    auto s = std::make_shared<UavSubscription>();
    if (!cb) {
        std::cerr << "cb is null" << std::endl;
        return {};
    }
    s->impl_ = impl_->create_subscription(topic, qos, cb);
    return s;
}

}