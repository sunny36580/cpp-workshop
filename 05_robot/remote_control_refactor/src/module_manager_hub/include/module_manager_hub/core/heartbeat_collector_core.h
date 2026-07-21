#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

// =====================================================================
// 二进制 UDP 上报协议（终版）
// 帧头  8B: Magic(2) + Ver(1) + Count(1) + Seq(1) + Reserved(1) + CRC32(4)
// 条目 12B: SvcID(2) + State(1) + Reserved(1) + LastHeartbeat(8)
//
// State 枚举:
//   0 = OFFLINE    1 = ONLINE     2 = STARTING
//   3 = STOPPING   4 = UNHEALTHY  5-255 = 保留
// =====================================================================
constexpr uint16_t UDP_MAGIC     = 0x4852;  // "HR"
constexpr uint8_t  UDP_VERSION   = 1;
constexpr size_t   UDP_HEADER_LEN = 8;
constexpr size_t   UDP_ENTRY_LEN  = 12;     // svc_id(2) + state(1) + reserved(1) + last_hb(8)

enum SvcState : uint8_t {
  SVC_OFFLINE   = 0,
  SVC_ONLINE    = 1,
  SVC_STARTING  = 2,
  SVC_STOPPING  = 3,
  SVC_UNHEALTHY = 4,
};

#pragma pack(push, 1)
struct UdpHeader {
  uint16_t magic;
  uint8_t  version;
  uint8_t  count;
  uint8_t  seq;
  uint8_t  reserved;
  uint32_t crc32;    // 覆盖整个包（CRC 字段本身填 0 后计算）
};

struct UdpEntry {
  uint16_t svc_id;
  uint8_t  state;
  uint8_t  reserved;
  double   last_hb;
};
#pragma pack(pop)

/// 回调类型：日志输出（由 Node 层注入）
using HbLogCallback = std::function<void(int level, const std::string& msg)>;

/// 心跳汇聚核心 —— 纯业务逻辑，不依赖 ROS
/// 逻辑 A：原子写入心跳文件 → Runtime 本地监控
/// 逻辑 B：二进制 UDP 上报远端中控
class HeartbeatCollectorCore {
public:
  struct HeartbeatTarget {
    std::string name;
    std::string topic;       // 仅标识用，实际订阅在 Node 层
    std::string msg_type;
    uint16_t svc_id = 0;
    double last_time = 0.0;
    uint8_t state = SVC_OFFLINE;
  };

  HeartbeatCollectorCore();
  ~HeartbeatCollectorCore();

  /// 设置日志回调
  void setLogCallback(HbLogCallback cb) { log_cb_ = std::move(cb); }

  /// 加载配置
  void loadConfig(const std::string& heartbeat_dir, double timeout_sec,
                  const std::string& udp_host, int udp_port,
                  const std::vector<HeartbeatTarget>& targets);

  /// 逻辑 A：原子写入心跳文件
  void writeHeartbeatFile(const std::string& name, double timestamp);

  /// 初始化 UDP socket
  void initUdpSocket();

  /// 逻辑 B：UDP 上报
  void sendUdpReport();

  /// 心跳接收（更新目标状态）
  void onHeartbeat(const std::string& name, double now);

  /// 超时检测
  void checkTimeout(double now);

  /// 获取所有心跳目标（Node 层读取用）
  const std::vector<HeartbeatTarget>& targets() const { return targets_; }
  std::vector<HeartbeatTarget>& targets() { return targets_; }

  /// CRC32 计算
  static uint32_t calcCRC32(const uint8_t* data, size_t len);

private:
  std::vector<HeartbeatTarget> targets_;
  std::string heartbeat_dir_;
  double timeout_sec_ = 8.0;

  // UDP 上报
  std::string udp_host_;
  int udp_port_ = 0;
  int udp_sock_ = -1;
  uint8_t seq_ = 0;

  // 日志回调
  HbLogCallback log_cb_;
};
