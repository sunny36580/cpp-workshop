/// @file test_heartbeat_core.cpp
/// 纯 C++ 单测（无 ROS）：HeartbeatCollectorCore

#include "module_manager_hub/core/heartbeat_collector_core.h"
#include <cstdio>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <unistd.h>

namespace fs = std::filesystem;

// =====================================================================
// 辅助：构造 HeartbeatTarget
// =====================================================================
static HeartbeatCollectorCore::HeartbeatTarget makeTarget(const std::string& name,
                                                           uint16_t svc_id) {
  HeartbeatCollectorCore::HeartbeatTarget t;
  t.name   = name;
  t.topic  = "/robot/" + name + "/heartbeat";
  t.svc_id = svc_id;
  return t;
}

// =====================================================================
// 测试用例
// =====================================================================

static void test_heartbeat_mark_online() {
  printf("[TEST] test_heartbeat_mark_online ... ");
  HeartbeatCollectorCore core;

  auto t = makeTarget("test_svc", 1);
  core.loadConfig("", 8.0, "", 0, {t});

  core.onHeartbeat("test_svc", 100.0);
  assert(core.targets().size() == 1);
  assert(core.targets()[0].state == SVC_ONLINE);
  assert(core.targets()[0].last_time == 100.0);

  printf("PASS\n");
}

static void test_heartbeat_timeout_offline() {
  printf("[TEST] test_heartbeat_timeout_offline ... ");
  HeartbeatCollectorCore core;

  auto t = makeTarget("svc_a", 1);
  core.loadConfig("", 5.0, "", 0, {t});

  core.onHeartbeat("svc_a", 100.0);
  assert(core.targets()[0].state == SVC_ONLINE);

  // 4 秒后：未超时
  core.checkTimeout(104.0);
  assert(core.targets()[0].state == SVC_ONLINE);

  // 再 2 秒后（共 6s > timeout=5s）：应超时
  core.checkTimeout(106.0);
  assert(core.targets()[0].state == SVC_OFFLINE);

  printf("PASS\n");
}

static void test_heartbeat_recover_after_timeout() {
  printf("[TEST] test_heartbeat_recover_after_timeout ... ");
  HeartbeatCollectorCore core;

  auto t = makeTarget("svc_b", 2);
  core.loadConfig("", 3.0, "", 0, {t});

  core.onHeartbeat("svc_b", 100.0);

  // 超时
  core.checkTimeout(104.0);
  assert(core.targets()[0].state == SVC_OFFLINE);

  // 恢复心跳
  core.onHeartbeat("svc_b", 105.0);
  assert(core.targets()[0].state == SVC_ONLINE);
  assert(core.targets()[0].last_time == 105.0);

  printf("PASS\n");
}

static void test_multiple_targets() {
  printf("[TEST] test_multiple_targets ... ");
  HeartbeatCollectorCore core;

  auto t1 = makeTarget("svc_a", 1);
  auto t2 = makeTarget("svc_b", 2);
  auto t3 = makeTarget("svc_c", 3);
  core.loadConfig("", 5.0, "", 0, {t1, t2, t3});

  core.onHeartbeat("svc_a", 100.0);
  core.onHeartbeat("svc_c", 100.0);
  // svc_b 没有心跳

  assert(core.targets()[0].state == SVC_ONLINE);
  assert(core.targets()[1].state == SVC_OFFLINE);
  assert(core.targets()[2].state == SVC_ONLINE);

  // 超时检测
  core.checkTimeout(110.0);
  assert(core.targets()[0].state == SVC_OFFLINE);  // svc_a 超时
  assert(core.targets()[1].state == SVC_OFFLINE);  // svc_b 仍离线
  assert(core.targets()[2].state == SVC_OFFLINE);  // svc_c 超时

  printf("PASS\n");
}

static void test_heartbeat_file_written() {
  printf("[TEST] test_heartbeat_file_written ... ");

  // 使用临时目录
  const std::string dir = "/tmp/hb_test_" + std::to_string(getpid());
  fs::remove_all(dir);

  HeartbeatCollectorCore core;
  auto t = makeTarget("test_svc", 1);
  core.loadConfig(dir, 8.0, "", 0, {t});

  core.onHeartbeat("test_svc", 1234.567);
  assert(fs::exists(dir + "/test_svc"));

  // 检查文件内容
  std::ifstream f(dir + "/test_svc");
  std::string content;
  std::getline(f, content);
  printf("  file content: '%s'\n", content.c_str());
  double val = std::stod(content);
  assert(std::abs(val - 1234.567) < 0.001);

  // 超时后文件应被清理
  core.checkTimeout(1300.0);
  assert(!fs::exists(dir + "/test_svc"));

  fs::remove_all(dir);
  printf("PASS\n");
}

static void test_crc32_correctness() {
  printf("[TEST] test_crc32_correctness ... ");

  // 已知值校验：空数据 CRC32 = 0x00000000
  uint32_t crc_empty = HeartbeatCollectorCore::calcCRC32(nullptr, 0);
  // 注：空指针传给 calcCRC32 会出问题，传一个空数组
  uint8_t empty = 0;
  crc_empty = HeartbeatCollectorCore::calcCRC32(&empty, 0);
  // CRC32("") = 0x00000000
  assert(crc_empty == 0x00000000);

  // "123456789" 的标准 CRC32 = 0xCBF43926
  const char* test_str = "123456789";
  uint32_t crc = HeartbeatCollectorCore::calcCRC32(
      reinterpret_cast<const uint8_t*>(test_str), 9);
  assert(crc == 0xCBF43926);

  printf("PASS\n");
}

static void test_svc_id_not_set() {
  printf("[TEST] test_svc_id_not_set ... ");
  HeartbeatCollectorCore core;

  // 默认 svc_id = 0
  HeartbeatCollectorCore::HeartbeatTarget t;
  t.name = "no_id";
  core.loadConfig("", 8.0, "", 0, {t});

  assert(core.targets()[0].svc_id == 0);
  assert(core.targets()[0].state == SVC_OFFLINE);

  // 收到心跳后应改为 ONLINE
  core.onHeartbeat("no_id", 42.0);
  assert(core.targets()[0].state == SVC_ONLINE);

  printf("PASS\n");
}

int main() {
  printf("===== HeartbeatCollectorCore Unit Tests =====\n\n");

  test_heartbeat_mark_online();
  test_heartbeat_timeout_offline();
  test_heartbeat_recover_after_timeout();
  test_multiple_targets();
  test_heartbeat_file_written();
  test_crc32_correctness();
  test_svc_id_not_set();

  printf("\n===== All %s tests PASSED =====\n", "HeartbeatCollectorCore");
  return 0;
}
