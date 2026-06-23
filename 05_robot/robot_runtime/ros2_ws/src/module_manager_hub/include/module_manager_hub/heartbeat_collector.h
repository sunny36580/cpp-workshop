#pragma once

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <yaml-cpp/yaml.h>

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

/// 心跳汇聚节点（整机状态汇总核心）
/// 逻辑 A：原子写入心跳文件 → Runtime 本地监控
/// 逻辑 B：二进制 UDP 上报远端中控
/// 自身心跳：/robot/collector/heartbeat
class HeartbeatCollector : public rclcpp::Node {
public:
  explicit HeartbeatCollector(const std::string &name,
                              const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
  ~HeartbeatCollector() override;

private:
  struct HeartbeatTarget {
    std::string name;
    std::string topic;
    std::string msg_type;
    uint16_t svc_id = 0;         // 二进制协议中的服务 ID
    double last_time = 0.0;
    uint8_t state = SVC_OFFLINE;
  };

  void loadConfig(const YAML::Node &root);

  // 逻辑 A：原子写心跳文件
  void writeHeartbeatFile(const std::string &name, double timestamp);

  // 逻辑 B：UDP 上报
  void initUdpSocket();
  void sendUdpReport();

  // 心跳接收 & 自身心跳
  void onHeartbeat(const std::string &name);
  void publishSelfHeartbeat();
  void checkTimer();

  // 心跳追踪
  std::vector<HeartbeatTarget> targets_;
  std::map<std::string, rclcpp::SubscriptionBase::SharedPtr> subs_;
  rclcpp::TimerBase::SharedPtr self_hb_timer_;
  rclcpp::TimerBase::SharedPtr check_timer_;
  rclcpp::TimerBase::SharedPtr udp_timer_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr self_hb_pub_;

  // 配置
  std::string heartbeat_dir_;   // 心跳文件目录（Runtime 读取用）
  double timeout_sec_ = 8.0;

  // UDP 上报
  std::string udp_host_;
  int udp_port_ = 0;
  int udp_sock_ = -1;
  uint8_t seq_ = 0;              // 包序列号

  // CRC32
  static uint32_t calcCRC32(const uint8_t *data, size_t len);
};
