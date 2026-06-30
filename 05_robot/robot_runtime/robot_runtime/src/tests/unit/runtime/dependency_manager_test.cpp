#include <gtest/gtest.h>

#include "runtime/process/dependency_manager/dependency_manager.h"
#include "runtime/process/dependency_manager/topological_sort.h"

using robot_runtime::TopologicalSort;
using robot_runtime::DependencyManager;

// ============================================================================
// TopologicalSort
// ============================================================================

TEST(TopologicalSortTest, SingleNode) {
    TopologicalSort::DepMap deps = {{"A", {}}};
    auto order = TopologicalSort::sort(deps);
    ASSERT_EQ(order.size(), 1);
    EXPECT_EQ(order[0], "A");
}

TEST(TopologicalSortTest, SimpleChain) {
    TopologicalSort::DepMap deps = {
        {"C", {"B"}},
        {"B", {"A"}},
        {"A", {}},
    };
    auto order = TopologicalSort::sort(deps);
    ASSERT_EQ(order.size(), 3);
    auto posA = std::find(order.begin(), order.end(), "A");
    auto posB = std::find(order.begin(), order.end(), "B");
    auto posC = std::find(order.begin(), order.end(), "C");
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posC);
}

TEST(TopologicalSortTest, DiamondDependency) {
    TopologicalSort::DepMap deps = {
        {"D", {"B", "C"}},
        {"B", {"A"}},
        {"C", {"A"}},
        {"A", {}},
    };
    auto order = TopologicalSort::sort(deps);
    ASSERT_EQ(order.size(), 4);
    auto posA = std::find(order.begin(), order.end(), "A");
    auto posB = std::find(order.begin(), order.end(), "B");
    auto posC = std::find(order.begin(), order.end(), "C");
    auto posD = std::find(order.begin(), order.end(), "D");
    EXPECT_LT(posA, posB);
    EXPECT_LT(posA, posC);
    EXPECT_LT(posB, posD);
    EXPECT_LT(posC, posD);
}

TEST(TopologicalSortTest, NoDependencies) {
    TopologicalSort::DepMap deps = {{"X", {}}, {"Y", {}}, {"Z", {}}};
    auto order = TopologicalSort::sort(deps);
    ASSERT_EQ(order.size(), 3);
}

TEST(TopologicalSortTest, CircularDependency) {
    // 当前 DFS 实现不会死循环，但顺序不可靠
    TopologicalSort::DepMap deps = {
        {"A", {"B"}},
        {"B", {"A"}},
    };
    auto order = TopologicalSort::sort(deps);
    EXPECT_EQ(order.size(), 2);
}

TEST(TopologicalSortTest, MissingDependency) {
    // 引用不存在的依赖节点
    // "B" 不在 deps 中 → dfs("B") 不会 push 任何节点
    // "A" 在 deps 中 → 遍历完 deps 后 push "A"
    TopologicalSort::DepMap deps = {
        {"A", {"B"}},
        {"C", {}},
    };
    auto order = TopologicalSort::sort(deps);
    EXPECT_EQ(order.size(), 2);
    // C (无依赖) 和 A (B不在map中，跳过) 都会被排序
    EXPECT_NE(std::find(order.begin(), order.end(), "A"), order.end());
    EXPECT_NE(std::find(order.begin(), order.end(), "C"), order.end());
}

// ============================================================================
// DependencyManager
// ============================================================================

TEST(DependencyManagerTest, ResolveStartOrder) {
    DependencyManager dm;
    dm.set_dependencies({
        {"slam", {"lidar_driver"}},
        {"navigation", {"slam"}},
        {"lidar_driver", {}},
    });
    auto order = dm.resolve_start_order();
    ASSERT_EQ(order.size(), 3);
    auto posLidar = std::find(order.begin(), order.end(), "lidar_driver");
    auto posSlam  = std::find(order.begin(), order.end(), "slam");
    auto posNav   = std::find(order.begin(), order.end(), "navigation");
    EXPECT_LT(posLidar, posSlam);
    EXPECT_LT(posSlam, posNav);
}

TEST(DependencyManagerTest, ResolveStopOrder) {
    DependencyManager dm;
    dm.set_dependencies({
        {"slam", {"lidar_driver"}},
        {"navigation", {"slam"}},
        {"lidar_driver", {}},
    });
    auto order = dm.resolve_stop_order();
    ASSERT_EQ(order.size(), 3);
    auto posLidar = std::find(order.begin(), order.end(), "lidar_driver");
    auto posSlam  = std::find(order.begin(), order.end(), "slam");
    auto posNav   = std::find(order.begin(), order.end(), "navigation");
    EXPECT_LT(posNav, posSlam);
    EXPECT_LT(posSlam, posLidar);
}

TEST(DependencyManagerTest, AddDependency) {
    DependencyManager dm;
    dm.add_dependency("B", {"A"});
    dm.add_dependency("A", {});
    auto order = dm.resolve_start_order();
    ASSERT_EQ(order.size(), 2);
    EXPECT_LT(
        std::find(order.begin(), order.end(), "A"),
        std::find(order.begin(), order.end(), "B")
    );
}
