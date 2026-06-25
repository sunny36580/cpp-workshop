/// @file test_camera_streamer_core.cpp
/// 纯 C++ 单测（无 ROS）：CameraStreamerCore
///
/// 测试内容：
///   - 帧缓存 setFrame / getFrame
///   - 日志回调注入
///   - H.264 编码器初始化 & 编码（需 FFmpeg 支持）
///   - TCP server 端口绑定
///   - 推流循环启停

#include "module_manager_hub/core/camera_streamer_core.h"
#include <cstdio>
#include <cassert>
#include <opencv2/opencv.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// =====================================================================
// 测试用例
// =====================================================================

static void test_frame_buffer() {
  printf("[TEST] test_frame_buffer ... ");
  CameraStreamerCore core;

  // 初始无帧
  cv::Mat frame;
  assert(core.getFrame(frame) == false);

  // 设置一帧
  cv::Mat test_img(480, 640, CV_8UC3, cv::Scalar(128, 128, 128));
  core.setFrame(test_img);

  // 读取
  assert(core.getFrame(frame) == true);
  assert(frame.rows == 480);
  assert(frame.cols == 640);
  assert(frame.type() == CV_8UC3);

  // 读取后帧仍保留（不是消费语义）
  cv::Mat frame2;
  assert(core.getFrame(frame2) == true);
  assert(frame2.rows == 480);

  printf("PASS\n");
}

static void test_frame_buffer_thread_safety() {
  printf("[TEST] test_frame_buffer_thread_safety ... ");
  CameraStreamerCore core;

  std::atomic<bool> running{true};

  // 写入线程：不断更新帧
  std::thread writer([&]() {
    for (int i = 0; i < 100 && running; i++) {
      cv::Mat img(480, 640, CV_8UC3, cv::Scalar(i % 256, i % 256, i % 256));
      core.setFrame(img);
      std::this_thread::yield();
    }
  });

  // 读取线程：不断读取帧
  std::thread reader([&]() {
    for (int i = 0; i < 100 && running; i++) {
      cv::Mat frame;
      core.getFrame(frame);
      std::this_thread::yield();
    }
  });

  writer.join();
  reader.join();

  printf("PASS\n");
}

static void test_log_callback() {
  printf("[TEST] test_log_callback ... ");
  CameraStreamerCore core;

  int log_count = 0;
  core.setLogCallback([&](int level, const std::string& msg) {
    log_count++;
    assert(level >= 0 && level <= 2);
    assert(!msg.empty());
  });

  // initEncoder 会触发日志（如果 FFmpeg 不可用也会触发 error 日志）
  bool ok = core.initEncoder(640, 480, 3000);
  // 不管 encoder 是否成功，回调至少被调用了一次
  assert(log_count > 0);
  printf("PASS (log_count=%d, encoder_ok=%d)\n", log_count, ok);
}

static void test_encoder_init_and_encode() {
  printf("[TEST] test_encoder_init_and_encode ... ");
  CameraStreamerCore core;

  bool ok = core.initEncoder(320, 240, 500);  // 低分辨率低码率
  if (!ok) {
    printf("SKIP (FFmpeg H.264 encoder not available)\n");
    return;
  }

  assert(core.width() == 320);
  assert(core.height() == 240);
  assert(core.bitrate() == 500);

  // 构造一张测试图像
  cv::Mat test_img(240, 320, CV_8UC3, cv::Scalar(64, 128, 192));
  // 加一些变化，让编码器有东西可编
  cv::rectangle(test_img, cv::Rect(50, 50, 100, 100), cv::Scalar(255, 0, 0), -1);

  // 编码为关键帧
  std::vector<uint8_t> encoded;
  bool enc_ok = core.encodeFrame(test_img, encoded, true);
  assert(enc_ok);
  assert(!encoded.empty());
  printf("  encoded size=%zu bytes\n", encoded.size());

  // 编码第二帧（非关键帧）
  enc_ok = core.encodeFrame(test_img, encoded, false);
  assert(enc_ok);
  assert(!encoded.empty());

  // cleanup 后不应崩溃
  core.cleanupEncoder();

  printf("PASS\n");
}

static void test_encoder_reinit() {
  printf("[TEST] test_encoder_reinit ... ");
  CameraStreamerCore core;

  bool ok = core.initEncoder(320, 240, 500);
  if (!ok) {
    printf("SKIP (FFmpeg H.264 encoder not available)\n");
    return;
  }

  // 重新初始化（不同分辨率）
  ok = core.initEncoder(640, 480, 1000);
  assert(ok);
  assert(core.width() == 640);
  assert(core.height() == 480);

  cv::Mat test_img(480, 640, CV_8UC3, cv::Scalar(0, 255, 0));
  std::vector<uint8_t> encoded;
  assert(core.encodeFrame(test_img, encoded, true));
  assert(!encoded.empty());

  printf("PASS\n");
}

static void test_tcp_server_bind() {
  printf("[TEST] test_tcp_server_bind ... ");
  CameraStreamerCore core;

  // 绑定有效端口
  bool ok = core.initTcpServer(18996);
  assert(ok);

  printf("PASS\n");
}

static void test_stream_loop_start_stop() {
  printf("[TEST] test_stream_loop_start_stop ... ");
  CameraStreamerCore core;

  bool enc_ok = core.initEncoder(320, 240, 500);
  if (!enc_ok) {
    printf("SKIP (encoder not available)\n");
    return;
  }
  assert(core.initTcpServer(18997));

  // 放一帧进去
  cv::Mat test_img(240, 320, CV_8UC3, cv::Scalar(100, 150, 200));
  core.setFrame(test_img);

  // 启动推流线程
  std::atomic<bool> running{true};
  std::thread t(&CameraStreamerCore::streamLoop, &core,
                std::ref(running),
                []() { return true; });

  // 连接客户端，让 accept 立即返回，随后 running=false 退出循环
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(18997);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  connect(sock, (struct sockaddr*)&addr, sizeof(addr));

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  running = false;
  close(sock);
  t.join();

  printf("PASS\n");
}

int main() {
  printf("===== CameraStreamerCore Unit Tests =====\n\n");

  test_frame_buffer();
  test_frame_buffer_thread_safety();
  test_log_callback();
  test_encoder_init_and_encode();
  test_encoder_reinit();
  test_tcp_server_bind();
  test_stream_loop_start_stop();

  printf("\n===== All %s tests PASSED =====\n", "CameraStreamerCore");
  return 0;
}
