/**
 * @file dependency_manager.h
 * @brief 依赖排序管理
 * @role runtime/process
 */
#pragma once

#include "runtime/process/dependency_manager/topological_sort.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

namespace robot_runtime {

/**
 * @class DependencyManager
 * @brief 服务依赖排序
 * @responsibility 根据依赖关系计算启停顺序
 */
class DependencyManager {
public:
    using DepMap = TopologicalSort::DepMap;

    /**
     * @brief 设置依赖关系图
     * @param deps 依赖关系图
     */
    void set_dependencies(const DepMap& deps) { deps_ = deps; }
    void add_dependency(const std::string& name, const std::vector<std::string>& deps) {
        deps_[name] = deps;
    }

    /**
     * @brief 计算启动顺序
     * @return 启动顺序（依赖在前）
     */
    std::vector<std::string> resolve_start_order() const {
        return TopologicalSort::sort(deps_);
    }

    /**
     * @brief 计算停止顺序
     * @return 停止顺序（被依赖者在前）
     */
    std::vector<std::string> resolve_stop_order() const {
        auto order = TopologicalSort::sort(deps_);
        std::reverse(order.begin(), order.end());
        return order;
    }

    const DepMap& dependencies() const { return deps_; }

private:
    DepMap deps_;  // 依赖关系表
};

} // namespace robot_runtime
