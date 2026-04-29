#ifndef QOS_H
#define QOS_H
namespace uav_comm {

enum class Reliability {
    BEST_EFFORT,
    RELIABLE
};

struct QoS {
    int depth = 10;
    Reliability reliability = Reliability::BEST_EFFORT;
};

} // namespace uav_comm

#endif // QOS_H