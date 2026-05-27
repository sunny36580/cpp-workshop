#pragma once
#include <string>
#include <vector>

// 模块完整状态机
enum class ModuleState {
  UNLOADED,    // 未加载
  LOADING,     // 正在启动
  READY,       // 初始化完成，就绪
  RUNNING,     // 正常运行
  ERROR,       // 运行出错
  STOPPED      // 已停止
};

struct Module {
  std::string name;
  std::string package_name;
  std::string node_name;
  std::string monitor_topic;
  bool enabled;
  bool auto_start;
  double heartbeat_timeout;
  
  // 超时配置
  double startup_timeout = 5.0;
  
  // 重启配置
  int max_restart_count = 3;
  std::vector<double> restart_intervals = {0.0, 1.0, 3.0};
  
  // 运行时状态
  ModuleState state = ModuleState::UNLOADED;
  double last_heartbeat = 0.0;
  double startup_time = 0.0;
  int pid = -1;
  int restart_count = 0;
  std::string error_message;

  // 启动方式标记
  bool is_launch_file = false;
  bool is_python_script = false;
  std::string script_path;
};