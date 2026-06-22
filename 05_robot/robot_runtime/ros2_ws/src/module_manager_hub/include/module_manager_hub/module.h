#pragma once
#include <string>

/// 被追踪模块的状态（由话题心跳驱动，不再管理进程生命周期）
struct TrackedModule {
  std::string name;
  std::string watch_topic;
  std::string watch_type;
  bool online = false;
  double last_msg_time = 0.0;
};