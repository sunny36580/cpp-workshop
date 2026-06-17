#pragma once

#include "manager/dependency_manager/topological_sort.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace robot_runtime {

class DependencyManager {
public:
    using DepMap = TopologicalSort::DepMap;

    void set_dependencies(const DepMap& deps) { deps_ = deps; }
    void add_dependency(const std::string& name, const std::vector<std::string>& deps) {
        deps_[name] = deps;
    }

    std::vector<std::string> resolve_start_order() const {
        return TopologicalSort::sort(deps_);
    }

    std::vector<std::string> resolve_stop_order() const {
        auto order = TopologicalSort::sort(deps_);
        std::reverse(order.begin(), order.end());
        return order;
    }

    const DepMap& dependencies() const { return deps_; }

private:
    DepMap deps_;
};

} // namespace robot_runtime
