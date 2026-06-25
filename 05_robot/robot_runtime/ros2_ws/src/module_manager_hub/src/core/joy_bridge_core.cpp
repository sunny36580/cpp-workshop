#include "module_manager_hub/core/joy_bridge_core.h"
#include <algorithm>
#include <cstring>

// 帧头 2 字节 (0xAA 0x55)，避免轴数据中的 0xAA 误同步
static constexpr size_t   SOF_BYTES = 2;
static constexpr size_t   CRC_BYTES = 2;
static constexpr size_t   MAX_AXES  = 16;  // 轴数合理性上限
static constexpr size_t   MAX_BTNS  = 64;  // 按钮数合理性上限

// =====================================================================
// CRC16-Modbus，与 Python 端 calc_crc16 一致
// =====================================================================
uint16_t JoyBridgeCore::calcCRC16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int j = 0; j < 8; j++) {
      if (crc & 0x0001)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }
  return crc;
}

JoyBridgeCore::JoyBridgeCore()
{
}

void JoyBridgeCore::reset()
{
  rx_buf_.clear();
  frame_count_ = 0;
}

std::vector<JoyFrameData> JoyBridgeCore::feedData(const uint8_t* data, size_t len)
{
  std::vector<JoyFrameData> results;
  rx_buf_.insert(rx_buf_.end(), data, data + len);

  JoyFrameData frame;
  while (tryParseFrame(frame)) {
    // tryParseFrame 返回 true 有两种情况：
    //   a) 成功解析一帧（frame 有内容）→ 加入 results
    //   b) CRC/校验失败，跳过字节继续搜索（frame 仍为空）→ 不加入
    if (!frame.axes.empty() || !frame.buttons.empty()) {
      results.push_back(std::move(frame));
    }
    frame = JoyFrameData{};
  }

  return results;
}

// =====================================================================
// Python 端数据格式（与 _read_joystick_raw 一致）：
//   [0xAA 0x55:2B][num_axes:1B][axis0:int16LE][axis1:int16LE]...
//   [num_btns:1B][btn0:uint8][btn1:uint8]...[CRC16:2B LE]
//   CRC 校验范围：SOF 之后的所有数据（从 num_axes 到最后一个按钮）
// =====================================================================
bool JoyBridgeCore::tryParseFrame(JoyFrameData& out)
{
  // 找帧头 0xAA 0x55 (2字节)
  if (rx_buf_.size() < SOF_BYTES) return false;

  const uint8_t sof_bytes[SOF_BYTES] = {0xAA, 0x55};
  auto it = std::search(rx_buf_.begin(), rx_buf_.end(),
                        sof_bytes, sof_bytes + SOF_BYTES);
  if (it == rx_buf_.end()) {
    // 未找到帧头，清空缓存
    rx_buf_.clear();
    return false;
  }

  // 丢掉帧头之前的垃圾数据
  if (it != rx_buf_.begin()) {
    rx_buf_.erase(rx_buf_.begin(), it);
  }

  // 至少需要 SOF(2) + num_axes(1) = 3 字节
  if (rx_buf_.size() < SOF_BYTES + 1) return false;

  uint8_t num_axes = rx_buf_[SOF_BYTES];

  // ---- 合理性校验 ----
  if (num_axes == 0 || num_axes > MAX_AXES) {
    // 无效轴数，跳过头两个字节继续搜索
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 1);
    return true;  // 继续尝试
  }

  // 最小帧: SOF(2) + num_axes(1) + axes(N*2) + num_btns(1)
  size_t min_len = SOF_BYTES + 1 + num_axes * 2 + 1;
  if (rx_buf_.size() < min_len) return false;

  // 读按钮数量
  size_t btn_off = SOF_BYTES + 1 + num_axes * 2;
  uint8_t num_btns = rx_buf_[btn_off];

  // ---- 按钮数合理性校验 ----
  if (num_btns > MAX_BTNS) {
    // 无效按钮数，说明帧同步丢失，跳过头两个字节
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 1);
    return true;
  }

  // 完整帧长（含 CRC16 2 字节）
  size_t frame_len = SOF_BYTES + 1 + num_axes * 2 + 1 + num_btns + CRC_BYTES;
  if (rx_buf_.size() < frame_len) return false;

  // ---- CRC16 校验 ----
  // CRC 校验范围：从 num_axes 到最后一个按钮（不含 CRC 本身）
  size_t crc_data_len = 1 + num_axes * 2 + 1 + num_btns;
  uint16_t recv_crc = static_cast<uint16_t>(rx_buf_[frame_len - 2]) |
                      (static_cast<uint16_t>(rx_buf_[frame_len - 1]) << 8);
  uint16_t calc_crc = calcCRC16(&rx_buf_[SOF_BYTES], crc_data_len);
  if (recv_crc != calc_crc) {
    // CRC 错误，可能是帧同步丢失，跳过一个字节继续搜索
    rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + 1);
    return true;
  }

  // 解析轴: int16 LE → float (-32767~32767 → -1.0~1.0)
  out.axes.reserve(num_axes);
  for (size_t i = 0; i < num_axes; i++) {
    size_t off = SOF_BYTES + 1 + i * 2;
    int16_t raw = static_cast<int16_t>(rx_buf_[off]) |
                  (static_cast<int16_t>(rx_buf_[off + 1]) << 8);
    float val = static_cast<float>(raw) / 32767.0f;
    val = std::max(-1.0f, std::min(1.0f, val));
    out.axes.push_back(val);
  }

  // 解析按钮
  out.buttons.reserve(num_btns);
  for (size_t i = 0; i < num_btns; i++) {
    size_t off = SOF_BYTES + 1 + num_axes * 2 + 1 + i;
    out.buttons.push_back(rx_buf_[off] ? 1 : 0);
  }

  // 移除已处理的帧
  rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + frame_len);
  frame_count_++;

  return true;  // 可能还有更多帧
}
