#include "module_manager_hub/module_manager.h"
#include "module_manager_hub/serial_joy_bridge.h"
#include "module_manager_hub/camera_streamer.h"
#include <rclcpp/rclcpp.hpp>
#include <signal.h>

/// 同一进程内运行三个 node，由 MultiThreadedExecutor 分配不同线程：
///   - ModuleManager  （机器人遥控串口，32B 固定帧协议）
///   - SerialJoyBridge（摇杆串口，变长帧 → /joy）
///   - CameraStreamer  （USB 相机 H.264 推流 → TCP 8888）
int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  // 忽略 SIGPIPE，防止 send() 写入已关闭 socket 时进程被杀死
  signal(SIGPIPE, SIG_IGN);

  // 配置文件路径由 launch 通过 --ros-args -p 传入（绝对路径）
  // 不用 parameter_overrides，否则会覆盖 launch 传入的值
  auto mgr  = std::make_shared<ModuleManager>("module_manager_hub");
  auto joy  = std::make_shared<SerialJoyBridge>("serial_joy_bridge");
  auto cam  = std::make_shared<CameraStreamer>("camera_streamer_node");

  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(mgr);
  exec.add_node(joy);
  exec.add_node(cam);
  exec.spin();

  rclcpp::shutdown();
  return 0;
}