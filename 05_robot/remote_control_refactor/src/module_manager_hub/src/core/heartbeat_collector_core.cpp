#include "module_manager_hub/core/heartbeat_collector_core.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace fs = std::filesystem;

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

uint32_t HeartbeatCollectorCore::calcCRC32(const uint8_t *data, size_t len) {
  if (!crc32_init) buildCRC32Table();
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFF;
}

// =====================================================================
// 构造 & 析构
// =====================================================================
HeartbeatCollectorCore::HeartbeatCollectorCore()
{
}

HeartbeatCollectorCore::~HeartbeatCollectorCore()
{
  if (udp_sock_ >= 0) close(udp_sock_);
}

// =====================================================================
// 配置加载
// =====================================================================
void HeartbeatCollectorCore::loadConfig(const std::string& heartbeat_dir,
                                         double timeout_sec,
                                         const std::string& udp_host,
                                         int udp_port,
                                         const std::vector<HeartbeatTarget>& targets)
{
  heartbeat_dir_ = heartbeat_dir;
  timeout_sec_ = timeout_sec;
  udp_host_ = udp_host;
  udp_port_ = udp_port;
  targets_ = targets;
}

// =====================================================================
// 逻辑 A：原子写入心跳文件
// 使用临时文件 + mv 原子替换，避免 Runtime 读取半截文件
// =====================================================================
void HeartbeatCollectorCore::writeHeartbeatFile(const std::string &name, double timestamp)
{
  if (heartbeat_dir_.empty()) return;
  try {
    fs::create_directories(heartbeat_dir_);
  } catch (...) { return; }

  std::string tmp_path = heartbeat_dir_ + "/." + name + ".tmp";
  std::string dst_path = heartbeat_dir_ + "/" + name;

  // 写临时文件（用 std::setprecision 保留足够精度）
  {
    std::ofstream f(tmp_path);
    if (!f) return;
    f.precision(15);
    f << timestamp << std::endl;
  }

  // POSIX rename() 原子替换
  if (::rename(tmp_path.c_str(), dst_path.c_str()) != 0) {
    if (log_cb_) log_cb_(1, std::string("心跳文件写入失败 ") + dst_path + ": " + std::strerror(errno));
    fs::remove(tmp_path);
  }
}

// =====================================================================
// 逻辑 B：UDP 上报整机状态至中控
// =====================================================================
void HeartbeatCollectorCore::initUdpSocket()
{
  if (udp_host_.empty() || udp_port_ <= 0) return;
  udp_sock_ = socket(AF_INET, SOCK_DGRAM, 0);
  if (udp_sock_ < 0) {
    if (log_cb_) log_cb_(1, "UDP socket 创建失败");
  }
}

void HeartbeatCollectorCore::sendUdpReport()
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
// 心跳接收
// =====================================================================
void HeartbeatCollectorCore::onHeartbeat(const std::string &name, double now)
{
  for (auto &t : targets_) {
    if (t.name == name) {
      t.last_time = now;
      t.state = SVC_ONLINE;
      writeHeartbeatFile(name, now);
      break;
    }
  }
}

// =====================================================================
// 超时检测
// =====================================================================
void HeartbeatCollectorCore::checkTimeout(double now)
{
  for (auto &t : targets_) {
    if (t.state == SVC_OFFLINE) continue;

    bool timed_out = (now - t.last_time) > timeout_sec_;
    if (timed_out) {
      if (log_cb_) {
        char buf[256];
        snprintf(buf, sizeof(buf), "心跳超时: %s (%.1fs 无更新)", t.name.c_str(), now - t.last_time);
        log_cb_(1, buf);
      }
      t.state = SVC_OFFLINE;

      // 清除心跳文件
      if (!heartbeat_dir_.empty()) {
        fs::remove(heartbeat_dir_ + "/" + t.name);
        fs::remove(heartbeat_dir_ + "/." + t.name + ".tmp");
      }
    }
  }
}
