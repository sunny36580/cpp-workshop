#include "module_manager_hub/ros/serial_joy_bridge_node.h"
#include "module_manager_hub/ros/camera_streamer_node.h"
#include "module_manager_hub/ros/heartbeat_collector_node.h"
#include <rclcpp/rclcpp.hpp>
#include <signal.h>

/// 同一进程内运行多个 node，由 MultiThreadedExecutor 分配不同线程：
///   - SerialJoyBridgeNode  （摇杆串口，变长帧 → /joy）
///   - CameraStreamerNode   （USB 相机 H.264 推流 → TCP 8888）
///   - HeartbeatCollectorNode（心跳汇聚 → 文件/UDP 上报）
///
/// ModuleManagerNode 暂不启动（串口遥控协议阶段二启用）
int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  // 忽略 SIGPIPE，防止 send() 写入已关闭 socket 时进程被杀死
  signal(SIGPIPE, SIG_IGN);

  auto joy = std::make_shared<SerialJoyBridgeNode>("serial_joy_bridge");
  auto cam = std::make_shared<CameraStreamerNode>("camera_streamer_node");
  auto hbc = std::make_shared<HeartbeatCollectorNode>("heartbeat_collector");

  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(joy);
  exec.add_node(cam);
  exec.add_node(hbc);
  exec.spin();

  rclcpp::shutdown();
  return 0;
}