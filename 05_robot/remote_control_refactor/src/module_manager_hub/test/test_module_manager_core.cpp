/// @file test_module_manager_core.cpp
/// 纯 C++ 单测（无 ROS）：ModuleManagerCore

#include "module_manager_hub/core/module_manager_core.h"
#include <cstdio>
#include <cstring>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>

// =====================================================================
// CRC16-Modbus 辅助（与 Core 内部算法一致）
// =====================================================================
static uint16_t crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else              crc >>= 1;
    }
  }
  return crc;
}

/// 构造一帧合法的 32 字节遥控指令
/// @param cmd_type  指令类型 (CMD_MOVE / CMD_TASK / CMD_HEARTBEAT)
/// @param payload   载荷数据（最多 PROTO_DATA_LEN=16 字节）
/// @param pay_len   载荷长度
/// @param out       输出完整 32 字节帧
static void build32Frame(uint8_t cmd_type,
                          const uint8_t* payload, size_t pay_len,
                          std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(32);
  out.push_back(0xAA);  // SOF0
  out.push_back(0x55);  // SOF1
  out.push_back(cmd_type);
  out.push_back(static_cast<uint8_t>(pay_len < 16 ? pay_len : 16));

  // Data 字段（16 字节，不足补零）
  uint8_t data_field[16] = {0};
  size_t copy_len = pay_len < 16 ? pay_len : 16;
  std::memcpy(data_field, payload, copy_len);
  out.insert(out.end(), data_field, data_field + 16);

  // Reserved 字段（10 字节，全零）
  out.insert(out.end(), 10, 0);

  // CRC16：从 CmdType 到 Reserved 末尾（共 28 字节）
  uint16_t crc = crc16(out.data() + 2, 28);
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));

  assert(out.size() == 32);
}

// =====================================================================
// 测试用例
// =====================================================================

static void test_parse_cmd_move() {
  printf("[TEST] test_parse_cmd_move ... ");
  ModuleManagerCore core;

  // CMD_MOVE: 2 个 float（linear, angular）= 8 字节
  float linear  = 0.5f;
  float angular = -0.3f;
  uint8_t payload[8];
  std::memcpy(payload, &linear, 4);
  std::memcpy(payload + 4, &angular, 4);

  std::vector<uint8_t> frame;
  build32Frame(CMD_MOVE, payload, 8, frame);

  auto results = core.feedSerialData(frame.data(), frame.size());

  assert(results.size() == 1);
  assert(results[0].first == CMD_MOVE);
  assert(results[0].second.size() == 8);  // pay_len=8 所以 payload 8 字节

  float parsed_linear, parsed_angular;
  std::memcpy(&parsed_linear,  results[0].second.data(), 4);
  std::memcpy(&parsed_angular, results[0].second.data() + 4, 4);
  assert(std::abs(parsed_linear - 0.5f) < 0.001f);
  assert(std::abs(parsed_angular - (-0.3f)) < 0.001f);

  printf("PASS\n");
}

static void test_parse_cmd_task() {
  printf("[TEST] test_parse_cmd_task ... ");
  ModuleManagerCore core;

  // CMD_TASK: task_id = 3（PERFORM）
  uint8_t payload[] = {3};
  std::vector<uint8_t> frame;
  build32Frame(CMD_TASK, payload, 1, frame);

  auto results = core.feedSerialData(frame.data(), frame.size());

  assert(results.size() == 1);
  assert(results[0].first == CMD_TASK);
  assert(results[0].second.size() == 1);
  assert(results[0].second[0] == 3);

  printf("PASS\n");
}

static void test_parse_cmd_heartbeat_emergency() {
  printf("[TEST] test_parse_cmd_heartbeat_emergency ... ");
  ModuleManagerCore core;

  // CMD_HEARTBEAT: 无载荷
  std::vector<uint8_t> frame;
  build32Frame(CMD_HEARTBEAT, nullptr, 0, frame);

  auto results = core.feedSerialData(frame.data(), frame.size());

  assert(results.size() == 1);
  assert(results[0].first == CMD_HEARTBEAT);

  printf("PASS\n");
}

static void test_crc16_mismatch_rejected() {
  printf("[TEST] test_crc16_mismatch_rejected ... ");
  ModuleManagerCore core;

  float linear = 0.0f, angular = 0.0f;
  uint8_t payload[8];
  std::memcpy(payload, &linear, 4);
  std::memcpy(payload + 4, &angular, 4);

  std::vector<uint8_t> frame;
  build32Frame(CMD_MOVE, payload, 8, frame);

  // 篡改一个字节
  frame[20] ^= 0xFF;

  auto results = core.feedSerialData(frame.data(), frame.size());
  assert(results.empty());

  printf("PASS\n");
}

static void test_multiple_frames_in_one() {
  printf("[TEST] test_multiple_frames_in_one ... ");
  ModuleManagerCore core;

  uint8_t task_payload[] = {5};
  std::vector<uint8_t> f1, f2;
  build32Frame(CMD_TASK, task_payload, 1, f1);
  build32Frame(CMD_HEARTBEAT, nullptr, 0, f2);

  // 拼接两帧
  std::vector<uint8_t> combined = f1;
  combined.insert(combined.end(), f2.begin(), f2.end());

  auto results = core.feedSerialData(combined.data(), combined.size());
  assert(results.size() == 2);
  assert(results[0].first == CMD_TASK);
  assert(results[1].first == CMD_HEARTBEAT);

  printf("PASS\n");
}

static void test_garbage_before_sof_skipped() {
  printf("[TEST] test_garbage_before_sof_skipped ... ");
  ModuleManagerCore core;

  uint8_t payload[] = {1};
  std::vector<uint8_t> frame;
  build32Frame(CMD_TASK, payload, 1, frame);

  // 前面加垃圾
  std::vector<uint8_t> noisy;
  noisy.push_back(0x00);
  noisy.push_back(0xFF);
  noisy.push_back(0xAA);  // 单个 0xAA 不构成 SOF
  noisy.insert(noisy.end(), frame.begin(), frame.end());

  auto results = core.feedSerialData(noisy.data(), noisy.size());
  assert(results.size() == 1);
  assert(results[0].first == CMD_TASK);

  printf("PASS\n");
}

static void test_build_serial_frame_roundtrip() {
  printf("[TEST] test_build_serial_frame_roundtrip ... ");
  ModuleManagerCore core;

  // 组帧 → 解析回环测试
  uint8_t original[] = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> frame;
  core.buildSerialFrame(CMD_STATUS, original, 4, frame);

  assert(frame.size() == 32);

  // 回环解析
  auto results = core.feedSerialData(frame.data(), frame.size());
  assert(results.size() == 1);
  assert(results[0].first == CMD_STATUS);
  assert(results[0].second.size() == 4);
  assert(std::memcmp(results[0].second.data(), original, 4) == 0);

  printf("PASS\n");
}

static void test_set_target_speed() {
  printf("[TEST] test_set_target_speed ... ");
  ModuleManagerCore core;

  core.setTargetSpeed(0.5f, -0.3f);
  assert(std::abs(core.targetLinear().load() - 0.5f) < 0.001f);
  assert(std::abs(core.targetAngular().load() - (-0.3f)) < 0.001f);

  // 测试截断（超出 LINEAR_MAX=0.6 / ANGULAR_MAX=1.0）
  core.setTargetSpeed(10.0f, 10.0f);
  assert(core.targetLinear().load() <= 0.6f);
  assert(core.targetAngular().load() <= 1.0f);

  printf("PASS\n");
}

static void test_speed_interpolation() {
  printf("[TEST] test_speed_interpolation ... ");
  ModuleManagerCore core;

  // 设置目标速度并启动插值线程
  core.setTargetSpeed(0.4f, 0.0f);
  core.startSpeedLoop();

  // 等待几轮插值
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  float cl = core.currentLinear();
  // 经过 ~50ms (4 次 80Hz 迭代)，速度应从 0 开始上升
  // LINEAR_ACCEL * dt = 0.2 * 0.0125 = 0.0025 每次迭代
  // 4 次迭代：~0.01
  assert(cl > 0.0f);
  assert(cl <= 0.4f);

  // 设回零
  core.setTargetSpeed(0.0f, 0.0f);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  core.stopSpeedLoop();
  printf("PASS (linear=%.4f)\n", cl);
}

static void test_module_heartbeat_tracking() {
  printf("[TEST] test_module_heartbeat_tracking ... ");
  ModuleManagerCore core;

  // 模拟 Node 层添加追踪模块
  TrackedModule tm;
  tm.name = "test_mod";
  tm.watch_topic = "/test/topic";
  core.trackedModules()["test_mod"] = tm;

  assert(core.trackedModules()["test_mod"].online == false);

  core.updateModuleHeartbeat("test_mod", 100.0);
  assert(core.trackedModules()["test_mod"].online == true);
  assert(core.trackedModules()["test_mod"].last_msg_time == 100.0);

  printf("PASS\n");
}

int main() {
  printf("===== ModuleManagerCore Unit Tests =====\n\n");

  test_parse_cmd_move();
  test_parse_cmd_task();
  test_parse_cmd_heartbeat_emergency();
  test_crc16_mismatch_rejected();
  test_multiple_frames_in_one();
  test_garbage_before_sof_skipped();
  test_build_serial_frame_roundtrip();
  test_set_target_speed();
  test_speed_interpolation();
  test_module_heartbeat_tracking();

  printf("\n===== All %s tests PASSED =====\n", "ModuleManagerCore");
  return 0;
}
