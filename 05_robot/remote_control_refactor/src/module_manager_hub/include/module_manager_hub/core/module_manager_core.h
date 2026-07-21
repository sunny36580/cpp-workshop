#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "module_manager_hub/core/module.h"

// 32 字节固定帧串口协议常量（机器人遥控协议）
constexpr uint8_t  PROTO_SOF0       = 0xAA;
constexpr uint8_t  PROTO_SOF1       = 0x55;
constexpr size_t   PROTO_HEADER_LEN = 4;    // SOF0 + SOF1 + CmdType + PayLen
constexpr size_t   PROTO_DATA_LEN   = 16;
constexpr size_t   PROTO_RESERVED   = 10;
constexpr size_t   PROTO_FRAME_LEN  = 32;

constexpr uint8_t CMD_MOVE       = 0x01;
constexpr uint8_t CMD_TASK       = 0x02;
constexpr uint8_t CMD_HEARTBEAT  = 0x03;
constexpr uint8_t CMD_STATUS     = 0x04;

struct CmdRoute {
  std::string topic;
  std::string msg_type;
};

/// 回调类型：日志输出
using MmLogCallback = std::function<void(int level, const std::string& msg)>;

/// 解析后的指令数据（Node 层据此发布 ROS 消息）
struct ParsedMoveCmd {
  float linear;
  float angular;
};

struct ParsedTaskCmd {
  uint8_t task_id;
};

struct ParsedHeartbeatCmd {
  // 急停信号，无附加数据
};

/// 模块管理器核心 —— 纯协议解析 + 速度插值逻辑，不依赖 ROS
/// - 32 字节固定帧协议解析
/// - 速度平滑插值
/// - 串口帧组帧
class ModuleManagerCore {
public:
  ModuleManagerCore();
  ~ModuleManagerCore();

  void setLogCallback(MmLogCallback cb) { log_cb_ = std::move(cb); }

  // ========== 32 字节固定帧协议解析 ==========
  /// 喂入原始串口数据，返回本次解析出的指令类型列表
  /// 返回值：pair<cmd_type, payload>
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>>
  feedSerialData(const uint8_t* data, size_t len);

  /// 组帧
  void buildSerialFrame(uint8_t cmd_type, const uint8_t* payload,
                        size_t payload_len, std::vector<uint8_t>& out_frame);

  // ========== 速度插值 ==========
  void startSpeedLoop();
  void stopSpeedLoop();
  void setTargetSpeed(float linear, float angular);
  float currentLinear() const { return current_linear_; }
  float currentAngular() const { return current_angular_; }
  std::atomic<float>& targetLinear() { return target_linear_; }
  std::atomic<float>& targetAngular() { return target_angular_; }

  // ========== CRC16 ==========
  static uint16_t calcCRC16(const uint8_t* data, size_t len);

  // ========== 模块状态追踪 ==========
  void updateModuleHeartbeat(const std::string& name, double now);
  const std::map<std::string, TrackedModule>& trackedModules() const { return tracked_modules_; }
  std::map<std::string, TrackedModule>& trackedModules() { return tracked_modules_; }

private:
  void interpolateSpeedLoop();

  // 帧解析
  std::vector<uint8_t> rx_frame_;
  uint8_t sof_marker_[2] = {PROTO_SOF0, PROTO_SOF1};

  // 模块追踪
  std::map<std::string, TrackedModule> tracked_modules_;

  // 速度插值
  static constexpr double SPEED_INTERPOLATE_HZ = 80.0;
  static constexpr double LINEAR_ACCEL  = 0.2;
  static constexpr double ANGULAR_ACCEL = 0.3;
  static constexpr double LINEAR_MAX    = 0.6;
  static constexpr double ANGULAR_MAX   = 1.0;

  std::thread speed_thread_;
  std::atomic<bool> speed_running_{false};
  std::atomic<float> target_linear_{0.0f};
  std::atomic<float> target_angular_{0.0f};
  float current_linear_  = 0.0f;
  float current_angular_ = 0.0f;

  MmLogCallback log_cb_;
};
