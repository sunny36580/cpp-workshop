#ifndef TOPICGRAPH_H
#define TOPICGRAPH_H
#include "core/QoS.h"
#include <string>
#include <vector>
#include <shared_mutex>
#include <unordered_map>
#include <memory>
#include <unordered_set>
#include <functional>

namespace uav_comm {

struct TopicInfo {
    std::string node_id;      // 节点唯一ID
    std::string topic;        // 主题名（支持通配符 */**）
    QoS qos;                  // 服务质量
    bool is_publisher = false;// true=发布者，false=订阅者
    long long ts = 0;         // 最后活跃时间戳

    // 去重标识（业务唯一键）
    bool operator==(const TopicInfo& other) const {
        return node_id == other.node_id && topic == other.topic && is_publisher == other.is_publisher;
    }
};

// TopicInfo 哈希函数（用于去重）
struct TopicInfoHash {
    size_t operator()(const TopicInfo& info) const {
        size_t h1 = std::hash<std::string>()(info.node_id);
        size_t h2 = std::hash<std::string>()(info.topic);
        size_t h3 = std::hash<bool>()(info.is_publisher);
        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

// Trie 树节点
struct TrieNode {
    std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
    // 单层通配符 * 节点
    std::unique_ptr<TrieNode> wild_single;
    // 多层通配符 ** 节点
    std::unique_ptr<TrieNode> wild_multi;
    // 当前节点存储的主题信息
    std::vector<TopicInfo> infos;
};

class TopicGraph {
public:
    TopicGraph() : root_(std::make_unique<TrieNode>()) {}

    void add(const TopicInfo& info);
    std::vector<TopicInfo> match(const std::string& pub_topic);
    bool has_match(const std::string& topic, const QoS& qos, bool is_pub);
    bool is_compatible(const QoS& pub, const QoS& sub);
    void print();
    void update_node_alive(const std::string& node_id);
    void cleanup_node(int timeout_ms);
    void clear();

    static long long now();

private:
    std::vector<std::string> split_topic(const std::string& topic);
    
    // 递归匹配（锁外执行）
    void match_recursive(TrieNode* node, 
                        const std::vector<std::string>& parts, 
                        size_t index,
                        std::unordered_set<TopicInfo, TopicInfoHash>& res_set);

    // 空字符串 → 递归收集所有节点
    void collect_all(TrieNode* node, std::unordered_set<TopicInfo, TopicInfoHash>& res_set);
    
    void clear_recursive(TrieNode* node);

private:
    std::unique_ptr<TrieNode> root_;
    std::shared_mutex mtx_;  // 读写锁
    std::unordered_map<std::string, long long> node_alive_;
};

} // namespace uav_comm

#endif // TOPICGRAPH_H