#pragma once
#include <string>

struct Module {
  std::string name;
  std::string package_name;
  std::string node_name;
  std::string monitor_topic;
  bool enabled;
  bool auto_start;
  double heartbeat_timeout;

  bool online = false;
  bool running = false;
  double last_heartbeat = 0.0;
  int pid = -1;
};