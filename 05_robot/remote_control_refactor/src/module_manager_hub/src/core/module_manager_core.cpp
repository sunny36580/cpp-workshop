#include "module_manager_hub/core/module_manager_core.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <thread>

using namespace std::chrono_literals;

// CRC16-Modbus
uint16_t ModuleManagerCore::calcCRC16(const uint8_t *data, size_t len)
{
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

// 构造 & 析构
ModuleManagerCore::ModuleManagerCore()
{
}

ModuleManagerCore::~ModuleManagerCore()
{
  stopSpeedLoop();
}

// 32 字节固定帧串口协议解析
std::vector<std::pair<uint8_t, std::vector<uint8_t>>>
ModuleManagerCore::feedSerialData(const uint8_t* data, size_t len)
{
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> results;
  rx_frame_.insert(rx_frame_.end(), data, data + len);

  while (true) {
    auto &buf = rx_frame_;
    if (buf.size() < PROTO_FRAME_LEN) break;

    auto sof_it = std::search(buf.begin(), buf.end(),
                              sof_marker_, sof_marker_ + 2);
    if (sof_it == buf.end()) { buf.clear(); break; }
    if (sof_it != buf.begin()) buf.erase(buf.begin(), sof_it);
    if (buf.size() < PROTO_FRAME_LEN) break;

    // CRC16 校验
    uint16_t recv_crc = static_cast<uint16_t>(buf[PROTO_FRAME_LEN - 2]) |
                        (static_cast<uint16_t>(buf[PROTO_FRAME_LEN - 1]) << 8);
    uint16_t calc_crc = calcCRC16(buf.data() + 2, PROTO_FRAME_LEN - 4);

    if (recv_crc != calc_crc) {
      if (log_cb_) log_cb_(1, "32B帧 CRC16 错误, 丢弃");
      buf.erase(buf.begin(), buf.begin() + PROTO_FRAME_LEN);
      continue;
    }

    uint8_t cmd_type = buf[2];
    uint8_t pay_len  = buf[3];
    std::vector<uint8_t> payload(buf.data() + PROTO_HEADER_LEN,
                                  buf.data() + PROTO_HEADER_LEN +
                                  std::min(pay_len, static_cast<uint8_t>(PROTO_DATA_LEN)));

    results.emplace_back(cmd_type, std::move(payload));
    buf.erase(buf.begin(), buf.begin() + PROTO_FRAME_LEN);
  }

  return results;
}

// 组帧（32 字节固定帧）
void ModuleManagerCore::buildSerialFrame(uint8_t cmd_type, const uint8_t *payload,
                                          size_t payload_len, std::vector<uint8_t> &out)
{
  out.clear();
  out.reserve(PROTO_FRAME_LEN);
  out.push_back(PROTO_SOF0);
  out.push_back(PROTO_SOF1);
  out.push_back(cmd_type);
  out.push_back(static_cast<uint8_t>(std::min(payload_len, PROTO_DATA_LEN)));

  uint8_t data_field[PROTO_DATA_LEN] = {0};
  size_t copy_len = std::min(payload_len, PROTO_DATA_LEN);
  std::memcpy(data_field, payload, copy_len);
  out.insert(out.end(), data_field, data_field + PROTO_DATA_LEN);

  uint8_t reserved[PROTO_RESERVED] = {0};
  out.insert(out.end(), reserved, reserved + PROTO_RESERVED);

  uint16_t crc = calcCRC16(out.data() + 2, PROTO_FRAME_LEN - 4);
  out.push_back(static_cast<uint8_t>(crc & 0xFF));
  out.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
}

// 速度插值
void ModuleManagerCore::startSpeedLoop()
{
  speed_running_ = true;
  speed_thread_ = std::thread(&ModuleManagerCore::interpolateSpeedLoop, this);
}

void ModuleManagerCore::stopSpeedLoop()
{
  speed_running_ = false;
  if (speed_thread_.joinable()) speed_thread_.join();
}

void ModuleManagerCore::setTargetSpeed(float linear, float angular)
{
  target_linear_.store(std::clamp(linear,  -static_cast<float>(LINEAR_MAX), static_cast<float>(LINEAR_MAX)),
                        std::memory_order_relaxed);
  target_angular_.store(std::clamp(angular, -static_cast<float>(ANGULAR_MAX), static_cast<float>(ANGULAR_MAX)),
                         std::memory_order_relaxed);
}

void ModuleManagerCore::interpolateSpeedLoop()
{
  // 这个函数只做纯数学计算，不发布 ROS 消息
  // Node 层通过 getCurrentSpeed 或回调获取结果
  // 实际发布在 Node 层完成
  constexpr double dt = 1.0 / SPEED_INTERPOLATE_HZ;

  while (speed_running_) {
    float tgt_linear  = target_linear_.load(std::memory_order_relaxed);
    float tgt_angular = target_angular_.load(std::memory_order_relaxed);

    if (std::abs(tgt_linear - current_linear_) < 0.001f) {
      current_linear_ = tgt_linear;
    } else {
      float step = LINEAR_ACCEL * dt;
      current_linear_ = (tgt_linear > current_linear_)
          ? std::min(current_linear_ + step, tgt_linear)
          : std::max(current_linear_ - step, tgt_linear);
    }

    if (std::abs(tgt_angular - current_angular_) < 0.001f) {
      current_angular_ = tgt_angular;
    } else {
      float step = ANGULAR_ACCEL * dt;
      current_angular_ = (tgt_angular > current_angular_)
          ? std::min(current_angular_ + step, tgt_angular)
          : std::max(current_angular_ - step, tgt_angular);
    }

    std::this_thread::sleep_for(std::chrono::duration<double>(dt));
  }
}

// 模块状态追踪
void ModuleManagerCore::updateModuleHeartbeat(const std::string& name, double now)
{
  auto it = tracked_modules_.find(name);
  if (it != tracked_modules_.end()) {
    it->second.last_msg_time = now;
    it->second.online = true;
  }
}
