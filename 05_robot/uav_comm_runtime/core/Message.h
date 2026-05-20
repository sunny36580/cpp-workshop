#ifndef MESSAGE_H
#define MESSAGE_H

#include <memory>
#include <vector>
#include <cstdint>
#include <cstring>

namespace uav_comm {

struct Message {
    using ptr = std::shared_ptr<Message>;

    std::vector<uint8_t> buffer;

    Message() = default;
    explicit Message(const std::vector<uint8_t>& data) : buffer(data) {}
    explicit Message(std::vector<uint8_t>&& data) : buffer(std::move(data)) {}

    static ptr create(size_t size) {
        auto msg = std::make_shared<Message>();
        msg->buffer.resize(size);
        return msg;
    }

    static ptr create(const void* data, size_t size) {
        auto msg = create(size);
        if (data && size) memcpy(msg->buffer.data(), data, size);
        return msg;
    }

    static ptr create(const std::vector<uint8_t>& data) {
        return std::make_shared<Message>(data);
    }

    static ptr create(std::vector<uint8_t>&& data) {
        return std::make_shared<Message>(std::move(data));
    }

    const uint8_t* data() const { return buffer.data(); }
    uint8_t* data() { return buffer.data(); }
    size_t size() const { return buffer.size(); }
    bool empty() const { return buffer.empty(); }
};

}

#endif // MESSAGE_H