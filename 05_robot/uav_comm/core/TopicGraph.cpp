#include "TopicGraph.h"
#include <iostream>
#include <algorithm>
#include <sstream>
#include <chrono>

namespace uav_comm {

long long TopicGraph::now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<std::string> TopicGraph::split_topic(const std::string& topic) {
    std::vector<std::string> parts;
    std::stringstream ss(topic);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (!part.empty()) parts.push_back(part);
    }
    return parts;
}

void TopicGraph::add(const TopicInfo& info) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto parts = split_topic(info.topic);
    TrieNode* cur = root_.get();

    for (auto& part : parts) {
        if (part == "*") {
            if (!cur->wild_single) cur->wild_single = std::make_unique<TrieNode>();
            cur = cur->wild_single.get();
        } else if (part == "**") {
            if (!cur->wild_multi) cur->wild_multi = std::make_unique<TrieNode>();
            cur = cur->wild_multi.get();
            break;
        } else {
            if (!cur->children.count(part)) cur->children[part] = std::make_unique<TrieNode>();
            cur = cur->children[part].get();
        }
    }

    // 更新/添加主题信息
    for (auto& i : cur->infos) {
        if (i == info) {
            i.ts = now();
            return;
        }
    }
    auto copy = info;
    copy.ts = now();
    cur->infos.push_back(copy);
}

// 递归收集所有节点（空字符串匹配全部）
void TopicGraph::collect_all(TrieNode* node, std::unordered_set<TopicInfo, TopicInfoHash>& res_set) {
    if (!node) return;
    for (const auto& info : node->infos) res_set.insert(info);
    // 递归所有子节点
    for (const auto& pair : node->children) collect_all(pair.second.get(), res_set);
    collect_all(node->wild_single.get(), res_set);
    collect_all(node->wild_multi.get(), res_set);
}

// ** 完整语义+ 结果去重
void TopicGraph::match_recursive(TrieNode* node, 
                                const std::vector<std::string>& parts, 
                                size_t index,
                                std::unordered_set<TopicInfo, TopicInfoHash>& res_set)
{
    if (!node) return;

    // 匹配到末尾：收集当前节点
    if (index == parts.size()) {
        for (const auto& info : node->infos) res_set.insert(info);
        return;
    }

    const std::string& cur_part = parts[index];

    // 1.多层通配符 ** 正确语义（匹配当前 + 吃掉所有后续层级）
    if (node->wild_multi) {
        // 匹配当前层级
        for (const auto& info : node->wild_multi->infos) res_set.insert(info);
        // 递归后续所有层级
        match_recursive(node->wild_multi.get(), parts, index + 1, res_set);
    }

    // 2. 单层通配符 *
    match_recursive(node->wild_single.get(), parts, index + 1, res_set);

    // 3. 精确匹配子节点
    auto it = node->children.find(cur_part);
    if (it != node->children.end()) {
        match_recursive(it->second.get(), parts, index + 1, res_set);
    }
}

// 最小锁范围 + 去重 + 空字符串全匹配
std::vector<TopicInfo> TopicGraph::match(const std::string& pub_topic) {
    // 用 unordered_set 自动去重
    std::unordered_set<TopicInfo, TopicInfoHash> res_set;
    TrieNode* root_snapshot = nullptr;
    std::vector<std::string> parts;

    // 共享锁（并发读，性能极高）
    {
        std::shared_lock<std::shared_mutex> lock(mtx_);
        root_snapshot = root_.get();
        
        // 空字符串 → 收集所有订阅/发布
        if (pub_topic.empty()) {
            collect_all(root_snapshot, res_set);
            return std::vector<TopicInfo>(res_set.begin(), res_set.end());
        }
        
        parts = split_topic(pub_topic);
    }

    //  核心匹配逻辑在锁外执行！无锁竞争
    match_recursive(root_snapshot, parts, 0, res_set);

    return std::vector<TopicInfo>(res_set.begin(), res_set.end());
}

bool TopicGraph::is_compatible(const QoS& pub, const QoS& sub) {
    if (pub.reliability == Reliability::RELIABLE) return true;
    return sub.reliability == Reliability::BEST_EFFORT;
}

bool TopicGraph::has_match(const std::string& pub_topic, const QoS& qos, bool is_pub) {
    auto infos = match(pub_topic);
    for (auto& i : infos) {
        if (is_pub && !i.is_publisher && is_compatible(qos, i.qos)) return true;
        if (!is_pub && i.is_publisher && is_compatible(i.qos, qos)) return true;
    }
    return false;
}

void TopicGraph::print() {
    std::shared_lock<std::shared_mutex> lock(mtx_);
    std::cout << "\n==== Topic Trie ====\n";
    auto all = match("");
    for (auto& i : all) {
        printf("[%s] %s | %s | %s\n",
               i.is_publisher ? "PUB" : "SUB",
               i.node_id.c_str(),
               i.topic.c_str(),
               i.qos.reliability == Reliability::RELIABLE ? "RELIABLE" : "BEST_EFFORT");
    }
    std::cout << "===================\n";
}

void TopicGraph::update_node_alive(const std::string& node_id) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    node_alive_[node_id] = now();
}

void TopicGraph::cleanup_node(int timeout_ms) {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    auto t = now();
    std::vector<std::string> dead_nodes;

    for (auto it = node_alive_.begin(); it != node_alive_.end();) {
        if (t - it->second > timeout_ms) {
            dead_nodes.push_back(it->first);
            it = node_alive_.erase(it);
        } else ++it;
    }

    // 清理 Trie 树中失效节点数据
    std::function<void(TrieNode*)> clear_dead = [&](TrieNode* node) {
        if (!node) return;
        node->infos.erase(
            std::remove_if(node->infos.begin(), node->infos.end(), [&](const TopicInfo& i) {
                return std::find(dead_nodes.begin(), dead_nodes.end(), i.node_id) != dead_nodes.end();
            }), node->infos.end()
        );
        for (auto& pair : node->children) clear_dead(pair.second.get());
        clear_dead(node->wild_single.get());
        clear_dead(node->wild_multi.get());
    };
    clear_dead(root_.get());
}

void TopicGraph::clear_recursive(TrieNode* node) {
    if (!node) return;
    node->infos.clear();
    for (auto& pair : node->children) clear_recursive(pair.second.get());
    clear_recursive(node->wild_single.get());
    clear_recursive(node->wild_multi.get());
}

void TopicGraph::clear() {
    std::lock_guard<std::shared_mutex> lock(mtx_);
    clear_recursive(root_.get());
    node_alive_.clear();
}

}