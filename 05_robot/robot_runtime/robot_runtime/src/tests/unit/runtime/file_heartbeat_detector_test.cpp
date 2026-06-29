#include <gtest/gtest.h>

#include "runtime/monitor/detector/file_heartbeat_detector.h"

using robot_runtime::FileHeartbeatDetector;

// ============================================================================
// FileHeartbeatDetector
// ============================================================================

TEST(FileHeartbeatDetectorTest, DefaultConstruct) {
    FileHeartbeatDetector detector;
    EXPECT_EQ(detector.timeout_sec(), 8.0);
    EXPECT_FALSE(detector.heartbeat_dir().empty());
}

TEST(FileHeartbeatDetectorTest, ConfigureTimeout) {
    FileHeartbeatDetector detector;
    detector.set_timeout_sec(3.0);
    EXPECT_EQ(detector.timeout_sec(), 3.0);
}

TEST(FileHeartbeatDetectorTest, ConfigureHeartbeatDir) {
    FileHeartbeatDetector detector;
    detector.set_heartbeat_dir("/tmp/test_hb");
    EXPECT_EQ(detector.heartbeat_dir(), "/tmp/test_hb");
}

TEST(FileHeartbeatDetectorTest, NonExistentServiceCheckReturnsTrue) {
    FileHeartbeatDetector detector;
    // 不存在的服务 → 无心跳文件 → check() 应返回 true（超时）
    EXPECT_TRUE(detector.check("nonexistent_service"));
}
