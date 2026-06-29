#include <gtest/gtest.h>

// command_dispatcher.h 依赖完整的 Runtime 定义
#include "core/runtime.h"
#include "gateway/tcp/command_dispatcher.h"

using namespace robot_runtime::net;

// ============================================================================
// CommandDispatcher
// ============================================================================

TEST(CommandDispatcherTest, HeaderCompiles) {
    SUCCEED();
}
