#include "module_manager_hub/serial_joy_bridge.h"
#include "module_manager_hub/camera_streamer.h"
#include "module_manager_hub/heartbeat_collector.h"
#include <rclcpp/rclcpp.hpp>
#include <signal.h>

/// 同一进程内运行多个 node，由 MultiThreadedExecutor 分配不同线程：
///   - SerialJoyBridge   （摇杆串口，变长帧 → /joy）
///   - CameraStreamer    （USB 相机 H.264 推流 → TCP 8888）
///   - HeartbeatCollector（心跳汇聚 → 文件/UDP 上报）
///
/// ModuleManager 暂不启动（串口遥控协议阶段二启用）
int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  // 忽略 SIGPIPE，防止 send() 写入已关闭 socket 时进程被杀死
  signal(SIGPIPE, SIG_IGN);

  auto joy = std::make_shared<SerialJoyBridge>("serial_joy_bridge");
  auto cam = std::make_shared<CameraStreamer>("camera_streamer_node");
  auto hbc = std::make_shared<HeartbeatCollector>("heartbeat_collector");

  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(joy);
  exec.add_node(cam);
  exec.add_node(hbc);
  exec.spin();

  rclcpp::shutdown();
  return 0;
}