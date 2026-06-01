#pragma once
#include <string>

/// 模块启动类型
enum class LaunchType {
  ROS2_RUN,        /// ros2 run <package> <node>
  ROS2_LAUNCH,     /// ros2 launch <package> <launch_file>
};

/// 脚本任务配置（UDP task 指令触发执行 .sh）
struct ScriptTask {
  std::string name;
  std::string description;
  std::string script_path;     // .sh 脚本路径（绝对路径）
  bool enabled = false;
};

/// 模块结构
struct Module {
  std::string name;
  LaunchType launch_type = LaunchType::ROS2_RUN;

  // ROS2 相关
  std::string package_name;     // ROS2 包名
  std::string node_name;        // 可执行文件名 或 launch 文件名
  std::string watch_topic;      // 监控的业务话题
  std::string working_dir;      // 工作目录（可选）

  // 控制参数
  bool enabled = false;
  bool auto_start = false;
  bool auto_restart = false;
  double timeout_sec = 5.0;

  // 运行时状态
  bool online = false;
  bool running = false;
  double last_msg_time = 0.0;
  int pid = 0;                  // 子进程 PID
};