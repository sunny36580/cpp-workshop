#ifndef ZENOH_TRANSPORT_H
#define ZENOH_TRANSPORT_H
#include "core/ITransport.h"
#include "core/Message.h"
#include <zenoh.hxx>
#include <memory>
#include <unordered_map>
#include <vector>
#include <iostream>

namespace uav_comm {

class ZenohTransport : public ITransport {
public:
    ZenohTransport() {
        zenoh::Config config = zenoh::Config::create_default();
        session_ = std::make_shared<zenoh::Session>(std::move(config));
    }

    ~ZenohTransport() override {
        subscribers_.clear();
    }

    void publish(const std::string& topic, Message::ptr msg) override {
        if (!msg) return;
        session_->put(zenoh::KeyExpr(topic), zenoh::Bytes(msg->buffer));
    }

    void subscribe(const std::string& topic, RawCallback cb) override {
        if (subscribers_.count(topic)) return; // 去重
        // std::cout << "[Zenoh] subscribe: " << topic << std::endl;

        auto sub = std::make_shared<zenoh::Subscriber<void>>(
            session_->declare_subscriber(
                zenoh::KeyExpr(topic),
                [cb](const zenoh::Sample& sample) {
                    // 转 vector → 构造 Message（移动语义，零拷贝）
                    std::vector<uint8_t> payload = sample.get_payload().as_vector();
                    cb(Message::create(std::move(payload)));
                },
                []() {}
            )
        );
        subscribers_[topic] = sub;
    }

    void unsubscribe(const std::string& topic) override {
        if (subscribers_.count(topic)) {
            subscribers_.erase(topic);
        }
    }

private:
    std::shared_ptr<zenoh::Session> session_;
    std::unordered_map<std::string, std::shared_ptr<zenoh::Subscriber<void>>> subscribers_;
};

}

#endif // ZENOH_TRANSPORT_H