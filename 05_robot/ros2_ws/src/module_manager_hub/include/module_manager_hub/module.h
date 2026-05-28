#pragma once
#include <string>

struct Module {
  std::string name;
  std::string node_name;       // 启动的节点名
  std::string watch_topic;     // 要监控的业务话题（无需心跳）
  bool enabled;
  bool auto_start;
  double timeout_sec;          // 超时判定秒

  bool online = false;
  bool running = false;
  double last_msg_time = 0.0;  // 最后一次收到数据时间
};