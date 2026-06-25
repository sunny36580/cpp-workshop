#pragma once

#include <cstdint>
#include <vector>
#include <functional>

/// 摇杆数据解析结果（无 ROS 依赖）
struct JoyFrameData {
  std::vector<float> axes;
  std::vector<int32_t> buttons;
};

/// 摇杆协议核心 —— 纯数据解析，不依赖 ROS
/// - 输入：原始串口字节流
/// - 输出：解析后的摇杆帧数据（axes / buttons）
class JoyBridgeCore {
public:
  JoyBridgeCore();

  /// 喂入原始串口数据，返回本次解析出的所有完整帧
  std::vector<JoyFrameData> feedData(const uint8_t* data, size_t len);

  /// 重置解析状态（清空接收缓存）
  void reset();

  /// 累计解析帧数（统计用）
  size_t frameCount() const { return frame_count_; }

private:
  bool tryParseFrame(JoyFrameData& out);

  static uint16_t calcCRC16(const uint8_t* data, size_t len);

  std::vector<uint8_t> rx_buf_;
  size_t frame_count_ = 0;
};
