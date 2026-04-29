#ifndef MESSAGEHEADER_H
#include <cstdint>

namespace uav_comm {

struct MessageHeader {
    uint64_t seq;
};

} // namespace uav_comm

#endif // MESSAGEHEADER_H