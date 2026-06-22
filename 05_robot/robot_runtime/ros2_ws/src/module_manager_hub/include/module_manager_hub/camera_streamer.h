#pragma once

#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/bool.hpp>
#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <cstdint>
#include <opencv2/core.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

/// USB 相机 H.264 推流节点
/// 订阅 /camera1/image_raw → libx264 编码 → TCP 8888 推流
class CameraStreamer : public rclcpp::Node
{
public:
    explicit CameraStreamer(const std::string& node_name,
                            const rclcpp::NodeOptions &opts = rclcpp::NodeOptions());
    ~CameraStreamer() override;

private:
    void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg);
    void initEncoder();
    void cleanupEncoder();
    bool encodeFrame(const cv::Mat &bgr, std::vector<uint8_t> &out, bool force_key);
    bool initTcpServer();
    void publishHeartbeat();
    void streamLoop();

    std::string image_topic_;
    int port_ = 8888;
    int bitrate_kbps_ = 3000;

    // 帧缓存
    cv::Mat latest_frame_;
    std::mutex frame_mutex_;

    // 编码器
    AVCodecContext *enc_ctx_ = nullptr;
    AVFrame *enc_frame_ = nullptr;
    AVPacket *enc_pkt_ = nullptr;
    SwsContext *sws_ctx_ = nullptr;
    int64_t pts_ = 0;
    int enc_width_ = 640, enc_height_ = 480;
    bool encoder_ready_ = false;

    // TCP
    int server_fd_ = -1;
    int client_fd_ = -1;

    // 线程
    std::thread stream_thread_;
    std::atomic<bool> running_{true};

    // ROS
    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr heartbeat_pub_;
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};
