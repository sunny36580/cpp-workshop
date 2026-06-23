#include "module_manager_hub/heartbeat_collector.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>

// =====================================================================
// CRC32 查找表 + 计算
// =====================================================================
static uint32_t crc32_table[256];
static bool crc32_init = false;

static void buildCRC32Table() {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t c = i;
    for (int j = 0; j < 8; j++) {
      if (c & 1) c = (c >> 1) ^ 0xEDB88320;
      else       c >>= 1;
    }
    crc32_table[i] = c;
  }
  crc32_init = true;
}

uint32_t HeartbeatCollector::calcCRC32(const uint8_t *data, size_t len) {
  if (!crc32_init) buildCRC32Table();
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFF;
}

namespace fs = std::filesystem;
using namespace std::chrono_literals;

// =====================================================================
// 构造 & 析构
// =====================================================================
HeartbeatCollector::HeartbeatCollector(const std::string &name,
                                       const rclcpp::NodeOptions &opts)
  : Node(name, opts)
{
  // 加载配置
  this->declare_parameter<std::string>("config_path", "");
  std::string cfg_path = this->get_parameter("config_path").as_string();
  if (!cfg_path.empty() && fs::exists(cfg_path)) {
    try {
      YAML::Node root = YAML::LoadFile(cfg_path);
      loadConfig(root);
    } catch (std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "加载配置文件失败: %s", e.what());
    }
  }

  // 没加载到配置时用测试默认值
  if (targets_.empty()) {
    HeartbeatTarget t;
    t.name = "test_service";
    t.topic = "/robot/test/heartbeat";
    t.msg_type = "std_msgs/msg/String";
    targets_.push_back(t);
  }

  // 自身心跳发布（已在 config 中声明为 target，svc_id=4）
  self_hb_pub_ = this->create_publisher<std_msgs::msg::String>(
      "/robot/collector/heartbeat", 10);

  // 为每个 target 建立订阅
  for (auto &t : targets_) {
    if (t.topic.empty()) continue;
    try {
      auto cb = [this, n = t.name](const std::shared_ptr<rclcpp::SerializedMessage>) {
        onHeartbeat(n);
      };
      std::string type = t.msg_type.empty() ? "std_msgs/msg/String" : t.msg_type;
      subs_[t.name] = this->create_generic_subscription(t.topic, type, 10, cb);
      RCLCPP_INFO(this->get_logger(), "心跳监听: %s ← %s [%s]",
                  t.name.c_str(), t.topic.c_str(), type.c_str());
    } catch (std::exception &e) {
      RCLCPP_WARN(this->get_logger(), "订阅失败 %s: %s", t.topic.c_str(), e.what());
    }
  }

  // UDP 初始化
  initUdpSocket();

  // 定时 1s 发布自身心跳
  self_hb_timer_ = this->create_wall_timer(1s,
      std::bind(&HeartbeatCollector::publishSelfHeartbeat, this));

  // 定时 2s 检查超时
  check_timer_ = this->create_wall_timer(2s,
      std::bind(&HeartbeatCollector::checkTimer, this));

  // 定时 2s UDP 上报
  udp_timer_ = this->create_wall_timer(2s,
      std::bind(&HeartbeatCollector::sendUdpReport, this));

  RCLCPP_INFO(this->get_logger(), "心跳汇聚节点启动 ✅");
}

HeartbeatCollector::~HeartbeatCollector()
{
  if (udp_sock_ >= 0) close(udp_sock_);
}

// =====================================================================
// 配置加载
// =====================================================================
void HeartbeatCollector::loadConfig(const YAML::Node &root)
{
  auto hb = root["heartbeat"];
  if (!hb) return;

  if (hb["heartbeat_dir"]) heartbeat_dir_ = hb["heartbeat_dir"].as<std::string>();
  if (hb["timeout_sec"])   timeout_sec_   = hb["timeout_sec"].as<double>();
  if (hb["udp_host"])      udp_host_      = hb["udp_host"].as<std::string>();
  if (hb["udp_port"])      udp_port_      = hb["udp_port"].as<int>();

  if (hb["targets"]) {
    for (const auto &entry : hb["targets"]) {
      HeartbeatTarget t;
      t.name      = entry["name"].as<std::string>();
      t.topic     = entry["topic"].as<std::string>();
      t.msg_type  = entry["msg_type"] ? entry["msg_type"].as<std::string>() : "std_msgs/msg/String";
      t.svc_id    = entry["svc_id"] ? entry["svc_id"].as<uint16_t>() : 0;
      targets_.push_back(t);
    }
  }
}

// =====================================================================
// 逻辑 A：原子写入心跳文件
// 使用临时文件 + mv 原子替换，避免 Runtime 读取半截文件
// =====================================================================
void HeartbeatCollector::writeHeartbeatFile(const std::string &name, double timestamp)
{
  if (heartbeat_dir_.empty()) return;
  try {
    fs::create_directories(heartbeat_dir_);
  } catch (...) { return; }

  std::string tmp_path = heartbeat_dir_ + "/." + name + ".tmp";
  std::string dst_path = heartbeat_dir_ + "/" + name;

  // 写临时文件
  {
    std::ofstream f(tmp_path);
    if (!f) return;
    f << timestamp << std::endl;
  }

  // POSIX rename() 原子替换，目标已存在时自动覆盖
  if (::rename(tmp_path.c_str(), dst_path.c_str()) != 0) {
    RCLCPP_WARN(this->get_logger(), "心跳文件写入失败 %s: %s",
                dst_path.c_str(), std::strerror(errno));
    fs::remove(tmp_path);
  }
}

// =====================================================================
// 逻辑 B：UDP 上报整机状态至中控
// =====================================================================
void HeartbeatCollector::initUdpSocket()
{
  if (udp_host_.empty() || udp_port_ <= 0) return;
  udp_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_sock_ < 0) {
    RCLCPP_WARN(this->get_logger(), "UDP socket 创建失败");
  }
}

void HeartbeatCollector::sendUdpReport()
{
  if (udp_sock_ < 0) return;

  uint8_t count = static_cast<uint8_t>(targets_.size());

  // 组包：Header + Entry[]
  std::vector<uint8_t> pkt(UDP_HEADER_LEN + count * UDP_ENTRY_LEN);

  // 填 header（CRC 先填 0）
  UdpHeader *hdr = reinterpret_cast<UdpHeader*>(pkt.data());
  hdr->magic    = htons(UDP_MAGIC);
  hdr->version  = UDP_VERSION;
  hdr->count    = count;
  hdr->seq      = seq_++;
  hdr->reserved = 0;
  hdr->crc32    = 0;

  // 填条目
  UdpEntry *entries = reinterpret_cast<UdpEntry*>(pkt.data() + UDP_HEADER_LEN);
  for (int i = 0; i < count; i++) {
    entries[i].svc_id   = htons(targets_[i].svc_id);
    entries[i].state    = targets_[i].state;
    entries[i].reserved = 0;
    entries[i].last_hb  = targets_[i].last_time;
  }

  // 计算 CRC32（覆盖整个包）
  hdr->crc32 = htonl(calcCRC32(pkt.data(), pkt.size()));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port   = htons(udp_port_);
  inet_pton(AF_INET, udp_host_.c_str(), &addr.sin_addr);

  sendto(udp_sock_, pkt.data(), pkt.size(), 0,
         (struct sockaddr*)&addr, sizeof(addr));
}

// =====================================================================
// 自身心跳发布
// =====================================================================
void HeartbeatCollector::publishSelfHeartbeat()
{
  auto msg = std_msgs::msg::String();
  msg.data = "alive";
  self_hb_pub_->publish(msg);
  // 更新自身在线状态（通过 onHeartbeat 入口统一，享受相同超时检测逻辑）
  onHeartbeat("heartbeat_collector");
}

// =====================================================================
// 心跳接收
// =====================================================================
void HeartbeatCollector::onHeartbeat(const std::string &name)
{
  for (auto &t : targets_) {
    if (t.name == name) {
      double now = this->now().seconds();
      t.last_time = now;
      t.state = SVC_ONLINE;

      // 逻辑 A：原子写入心跳文件
      writeHeartbeatFile(name, now);
      break;
    }
  }
}

// =====================================================================
// 超时检测
// =====================================================================
void HeartbeatCollector::checkTimer()
{
  double now = this->now().seconds();

  for (auto &t : targets_) {
    if (t.state == SVC_OFFLINE) continue;

    bool timed_out = (now - t.last_time) > timeout_sec_;
    if (timed_out) {
      RCLCPP_WARN(this->get_logger(), "心跳超时: %s (%.1fs 无更新)",
                  t.name.c_str(), now - t.last_time);
      t.state = SVC_OFFLINE;

      // 清除心跳文件
      if (!heartbeat_dir_.empty()) {
        fs::remove(heartbeat_dir_ + "/" + t.name);
        fs::remove(heartbeat_dir_ + "/." + t.name + ".tmp");
      }
    }
  }
}
