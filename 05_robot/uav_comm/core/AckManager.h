#ifndef ACKMANAGER_H
#define ACKMANAGER_H
#include <map>       // 替换unordered_map，有序存储seq
#include <vector>
#include <mutex>
#include <cstdint>

namespace uav_comm {

// 工业级缓存Key：writer_id(全局唯一) + seq(局部唯一)
// 解决：节点重启/重连 seq 重复导致的脏数据
struct CacheKey {
    uint64_t writer_id;  // 发布端唯一ID（全局）
    uint64_t seq;        // 消息序列号（局部）

    bool operator==(const CacheKey& other) const {
        return writer_id == other.writer_id && seq == other.seq;
    }
};

// 哈希函数（支持unordered_map）
struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const {
        return (std::hash<uint64_t>()(key.writer_id) << 1) ^ std::hash<uint64_t>()(key.seq);
    }
};


//仅缓存历史消息，等待 NACK 请求重传
class AckManager {
public:
    // 最大缓存64条（RTPS窗口大小）
    static constexpr size_t MAX_CACHE = 64;

    // 缓存带序列号的消息
    void add(uint64_t writer_id, uint64_t seq, const std::vector<uint8_t>& data) {
        std::lock_guard<std::mutex> lock(mtx_);
        CacheKey key{writer_id, seq};
        cache_[key] = data;
        if (seq > max_seq_) max_seq_ = seq;

        // 淘汰旧数据
        if (cache_.size() > MAX_CACHE) {
            uint64_t min_seq = seq - MAX_CACHE;
            for (auto it = cache_.begin(); it != cache_.end();) {
                if (it->first.seq < min_seq) {
                    it = cache_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // 获取需要重传的消息（NACK 触发）
    std::vector<std::vector<uint8_t>> get_resend(uint64_t writer_id, uint64_t start, uint64_t end) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::vector<std::vector<uint8_t>> res;
        for (uint64_t s = start; s <= end; ++s) {
            CacheKey key{writer_id, s};
            auto it = cache_.find(key);
            if (it != cache_.end()) {
                res.push_back(it->second);
            }
        }
        return res;
    }

    uint64_t get_max_seq() const { return max_seq_; }

private:
    mutable std::mutex mtx_;
    std::unordered_map<CacheKey, std::vector<uint8_t>, CacheKeyHash> cache_;
    uint64_t max_seq_ = 0;
};

} // namespace uav_comm
#endif