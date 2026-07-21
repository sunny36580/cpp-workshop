/// @file test_joy_bridge_core.cpp
/// 纯 C++ 单测（无 ROS）：JoyBridgeCore 协议解析

#include "module_manager_hub/core/joy_bridge_core.h"
#include <cstdio>
#include <cstring>
#include <cassert>

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

/// 构造一帧合法的摇杆数据
/// @param axes      每个轴的值（-1.0 ~ 1.0，将转换为 int16 LE）
/// @param buttons   每个按钮 0/1
/// @param out       输出完整帧（含 SOF + CRC）
static void buildJoyFrame(const float* axes, size_t num_axes,
                          const uint8_t* buttons, size_t num_btns,
                          std::vector<uint8_t>& out) {
  out.clear();
  out.push_back(0xAA);
  out.push_back(0x55);
  out.push_back(static_cast<uint8_t>(num_axes));

  for (size_t i = 0; i < num_axes; i++) {
    int16_t raw = static_cast<int16_t>(axes[i] * 32767.0f);
    out.push_back(static_cast<uint8_t>(raw & 0xFF));
    out.push_back(static_cast<uint8_t>((raw >> 8) & 0xFF));
  }

  out.push_back(static_cast<uint8_t>(num_btns));
  out.insert(out.end(), buttons, buttons + num_btns);

  // CRC16：从 num_axes 到最后一个按钮
  uint16_t crc = crc16(out.data() + 2, out.size() - 2);
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

// =====================================================================
// 测试用例
// =====================================================================

static void test_parse_valid_frame() {
  printf("[TEST] test_parse_valid_frame ... ");
  JoyBridgeCore core;

  float axes[] = {0.5f, -0.5f};
  uint8_t btns[] = {1, 0, 1, 0};

  std::vector<uint8_t> frame;
  buildJoyFrame(axes, 2, btns, 4, frame);

  auto results = core.feedData(frame.data(), frame.size());

  assert(results.size() == 1);
  assert(results[0].axes.size() == 2);
  assert(results[0].buttons.size() == 4);
  assert(results[0].axes[0] > 0.49f && results[0].axes[0] < 0.51f);
  assert(results[0].axes[1] > -0.51f && results[0].axes[1] < -0.49f);
  assert(results[0].buttons[0] == 1);
  assert(results[0].buttons[1] == 0);
  assert(results[0].buttons[2] == 1);
  assert(results[0].buttons[3] == 0);
  assert(core.frameCount() == 1);

  printf("PASS\n");
}

static void test_parse_multiple_frames() {
  printf("[TEST] test_parse_multiple_frames ... ");
  JoyBridgeCore core;

  // 构造两帧数据，拼在一起发送
  float axes1[] = {0.0f, 0.0f};
  uint8_t btns1[] = {1};
  std::vector<uint8_t> f1;
  buildJoyFrame(axes1, 2, btns1, 1, f1);

  float axes2[] = {1.0f, -1.0f};
  uint8_t btns2[] = {0, 1};
  std::vector<uint8_t> f2;
  buildJoyFrame(axes2, 2, btns2, 2, f2);

  // 合并发送
  std::vector<uint8_t> combined = f1;
  combined.insert(combined.end(), f2.begin(), f2.end());

  auto results = core.feedData(combined.data(), combined.size());

  assert(results.size() == 2);
  assert(results[0].axes.size() == 2);
  assert(results[1].axes.size() == 2);
  assert(results[0].buttons.size() == 1);
  assert(results[1].buttons.size() == 2);
  assert(core.frameCount() == 2);

  printf("PASS\n");
}

static void test_reject_bad_crc() {
  printf("[TEST] test_reject_bad_crc ... ");
  JoyBridgeCore core;

  float axes[] = {0.0f};
  uint8_t btns[] = {1};
  std::vector<uint8_t> frame;
  buildJoyFrame(axes, 1, btns, 1, frame);

  // 篡改 CRC 字节
  frame.back() ^= 0xFF;

  auto results = core.feedData(frame.data(), frame.size());
  assert(results.empty());
  assert(core.frameCount() == 0);

  printf("PASS\n");
}

static void test_reject_too_many_axes() {
  printf("[TEST] test_reject_too_many_axes ... ");
  JoyBridgeCore core;

  // num_axes=0 或 >16 应被拒绝
  uint8_t bad_frame[] = {0xAA, 0x55, 0x00, 0x00, 0x00, 0x00};
  // CRC 不重要，num_axes=0 会在 CRC 校验前被拒绝
  auto results = core.feedData(bad_frame, sizeof(bad_frame));
  assert(results.empty());

  // 恢复后可继续接收正常帧
  core.reset();

  float axes[] = {0.0f};
  uint8_t btns[] = {1};
  std::vector<uint8_t> good;
  buildJoyFrame(axes, 1, btns, 1, good);
  results = core.feedData(good.data(), good.size());
  assert(results.size() == 1);

  printf("PASS\n");
}

static void test_partial_data_accumulates() {
  printf("[TEST] test_partial_data_accumulates ... ");
  JoyBridgeCore core;

  float axes[] = {0.0f};
  uint8_t btns[] = {1};
  std::vector<uint8_t> frame;
  buildJoyFrame(axes, 1, btns, 1, frame);

  // 分两次发送：先发一半，再发另一半
  size_t half = frame.size() / 2;
  auto r1 = core.feedData(frame.data(), half);
  assert(r1.empty());  // 数据不足，还不能解析

  auto r2 = core.feedData(frame.data() + half, frame.size() - half);
  assert(r2.size() == 1);  // 第二次数据补齐后成功解析

  printf("PASS\n");
}

static void test_garbage_prefix_skipped() {
  printf("[TEST] test_garbage_prefix_skipped ... ");
  JoyBridgeCore core;

  // 在合法帧前面加垃圾字节
  float axes[] = {0.5f};
  uint8_t btns[] = {0};
  std::vector<uint8_t> frame;
  buildJoyFrame(axes, 1, btns, 1, frame);

  std::vector<uint8_t> noisy;
  noisy.push_back(0xFF);
  noisy.push_back(0x00);
  noisy.insert(noisy.end(), frame.begin(), frame.end());

  auto results = core.feedData(noisy.data(), noisy.size());
  assert(results.size() == 1);
  assert(results[0].axes.size() == 1);
  assert(results[0].axes[0] > 0.49f);

  printf("PASS\n");
}

static void test_reset_clears_state() {
  printf("[TEST] test_reset_clears_state ... ");
  JoyBridgeCore core;

  float axes[] = {0.0f};
  uint8_t btns[] = {1};
  std::vector<uint8_t> frame;
  buildJoyFrame(axes, 1, btns, 1, frame);

  core.feedData(frame.data(), frame.size());
  assert(core.frameCount() == 1);

  core.reset();
  assert(core.frameCount() == 0);

  // 重置后仍可正常解析
  auto results = core.feedData(frame.data(), frame.size());
  assert(results.size() == 1);
  assert(core.frameCount() == 1);

  printf("PASS\n");
}

int main() {
  printf("===== JoyBridgeCore Unit Tests =====\n\n");

  test_parse_valid_frame();
  test_parse_multiple_frames();
  test_reject_bad_crc();
  test_reject_too_many_axes();
  test_partial_data_accumulates();
  test_garbage_prefix_skipped();
  test_reset_clears_state();

  printf("\n===== All %s tests PASSED =====\n", "JoyBridgeCore");
  return 0;
}
