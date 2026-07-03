/**
 * @file topological_sort.h
 * @brief 基于 DFS 的拓扑排序
 * @role runtime/process
 */
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace robot_runtime {

/**
 * @brief 基于 DFS 的拓扑排序器
 */
class TopologicalSort {
public:
    using DepMap = std::unordered_map<std::string, std::vector<std::string>>;

    /**
     * @brief 执行拓扑排序
     * @param deps 依赖关系图
     * @return 排序后的节点列表（不检测循环依赖）
     */
    static std::vector<std::string> sort(const DepMap& deps) {
        std::vector<std::string> order;
        std::unordered_map<std::string, bool> visited;

        std::function<void(const std::string&)> dfs = [&](const std::string& name) {
            if (visited[name]) return;
            visited[name] = true;
            auto it = deps.find(name);
            if (it != deps.end()) {
                for (const auto& dep : it->second) {
                    dfs(dep);
                }
                order.push_back(name);
            }
        };

        for (const auto& [name, _] : deps) {
            dfs(name);
        }
        return order;
    }
};

} // namespace robot_runtime
