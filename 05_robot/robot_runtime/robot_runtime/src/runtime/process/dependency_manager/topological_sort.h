#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace robot_runtime {

class TopologicalSort {
public:
    using DepMap = std::unordered_map<std::string, std::vector<std::string>>;

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
