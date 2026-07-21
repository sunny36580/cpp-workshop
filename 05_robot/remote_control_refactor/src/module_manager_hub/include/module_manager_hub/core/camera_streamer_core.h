#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/core.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

/// 回调类型：日志输出（由 Node 层注入，转发到 RCLCPP）
using CoreLogCallback = std::function<void(int level, const std::string& msg)>;

/// 相机推流核心 —— 纯编码 + TCP 传输逻辑，不依赖 ROS
/// - 编码器管理（libx264）
/// - TCP server 管理
/// - 推流主循环
class CameraStreamerCore {
public:
  CameraStreamerCore();
  ~CameraStreamerCore();

  /// 设置日志回调（Node 层注入）
  void setLogCallback(CoreLogCallback cb) { log_cb_ = std::move(cb); }

  /// 初始化 H.264 编码器
  bool initEncoder(int width, int height, int bitrate_kbps);

  /// 清理编码器
  void cleanupEncoder();

  /// 编码一帧 BGR 图像为 H.264
  bool encodeFrame(const cv::Mat& bgr, std::vector<uint8_t>& out, bool force_key);

  /// 初始化 TCP server
  bool initTcpServer(int port);

  /// 更新最新帧（线程安全，由图像回调调用）
  void setFrame(const cv::Mat& frame);

  /// 获取最新帧（线程安全）
  bool getFrame(cv::Mat& frame);

  /// 推流主循环（阻塞运行，直到 running 被置 false）
  void streamLoop(std::atomic<bool>& running,
                  std::function<bool()> ok_check = nullptr);

  /// 获取当前编码器参数（用于日志）
  int width() const { return enc_width_; }
  int height() const { return enc_height_; }
  int bitrate() const { return bitrate_kbps_; }
  AVRational framerate() const { return enc_ctx_ ? enc_ctx_->framerate : AVRational{0, 0}; }

private:
  void logInfo(const std::string& msg);
  void logError(const std::string& msg);
  void logWarn(const std::string& msg);

  // 编码器
  AVCodecContext* enc_ctx_ = nullptr;
  AVFrame* enc_frame_ = nullptr;
  AVPacket* enc_pkt_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;
  int64_t pts_ = 0;
  int enc_width_ = 640, enc_height_ = 480;
  int bitrate_kbps_ = 3000;
  bool encoder_ready_ = false;

  // TCP
  int server_fd_ = -1;
  int client_fd_ = -1;
  int port_ = 8888;

  // 帧缓存
  cv::Mat latest_frame_;
  std::mutex frame_mutex_;

  // 日志回调
  CoreLogCallback log_cb_;
};
