#include <gtest/gtest.h>

#include "orchestration/mode/mode_manager.h"

using robot_runtime::ModeManager;

// ============================================================================
// ModeManager — 需要配置文件和 ServiceManager 才能完整构造
// 这里先做一个桩测试，验证头文件可编译、接口可调用
// ============================================================================

TEST(ModeManagerTest, HeaderCompiles) {
    // 链接正确、头文件可访问即通过
    SUCCEED();
}
