#include <gtest/gtest.h>

#include "runtime/process/dependency_manager/topological_sort.h"

using robot_runtime::TopologicalSort;

// ============================================================================
// TopologicalSort
// ============================================================================

TEST(TopologicalSortTest, SingleNode) {
    TopologicalSort::DepMap deps = {
        {"A", {}},
    };
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
    // A must come before B, B must come before C
    auto posA = std::find(order.begin(), order.end(), "A");
    auto posB = std::find(order.begin(), order.end(), "B");
    auto posC = std::find(order.begin(), order.end(), "C");
    EXPECT_LT(posA, posB);
    EXPECT_LT(posB, posC);
}

TEST(TopologicalSortTest, DiamondDependency) {
    //    A
    //   / \
    //  B   C
    //   \ /
    //    D
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
    TopologicalSort::DepMap deps = {
        {"X", {}},
        {"Y", {}},
        {"Z", {}},
    };
    auto order = TopologicalSort::sort(deps);
    ASSERT_EQ(order.size(), 3);
}
