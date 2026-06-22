#include "module_manager_hub/camera_streamer.h"
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_msgs/msg/bool.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <mutex>
#include <sys/poll.h>
#include <signal.h>
#include <cstring>
#include <cerrno>

using namespace std::chrono_literals;

CameraStreamer::CameraStreamer(const std::string& node_name,
                               const rclcpp::NodeOptions &opts)
    : Node(node_name, opts)
    {
        this->declare_parameter<std::string>("image_topic", "/camera1/image_raw");
        this->declare_parameter<int>("port", 8888);
        this->declare_parameter<int>("bitrate", 3000);

        image_topic_ = this->get_parameter("image_topic").as_string();
        port_        = this->get_parameter("port").as_int();
        bitrate_kbps_ = this->get_parameter("bitrate").as_int();

        heartbeat_pub_ = this->create_publisher<std_msgs::msg::Bool>("/camera/status", 10);
        heartbeat_timer_ = this->create_wall_timer(
            500ms, std::bind(&CameraStreamer::publishHeartbeat, this));

        image_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
            image_topic_, rclcpp::SensorDataQoS(),
            std::bind(&CameraStreamer::imageCallback, this, std::placeholders::_1));

        // 用默认尺寸初始化编码器
        enc_width_ = 640;
        enc_height_ = 480;
        initEncoder();
        initTcpServer();

        stream_thread_ = std::thread(&CameraStreamer::streamLoop, this);

        RCLCPP_INFO(this->get_logger(), "✅ 推流节点启动  topic=%s  port=%d  H.264",
                    image_topic_.c_str(), port_);
    }

CameraStreamer::~CameraStreamer()
{
    running_ = false;
    if (server_fd_ > 0) { close(server_fd_); server_fd_ = -1; }
    if (stream_thread_.joinable()) stream_thread_.join();
    if (client_fd_ > 0) { close(client_fd_); client_fd_ = -1; }
    cleanupEncoder();
}

// =====================================================================
// 以下为 CameraStreamer 成员函数实现
// =====================================================================

void CameraStreamer::imageCallback(const sensor_msgs::msg::Image::SharedPtr msg)
{
    try {
        auto cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_ = cv_ptr->image.clone();
    } catch (cv_bridge::Exception &e) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "cv_bridge failed: %s", e.what());
    }
}

void CameraStreamer::initEncoder()
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) { RCLCPP_ERROR(this->get_logger(), "找不到 H.264 编码器"); return; }
    enc_ctx_ = avcodec_alloc_context3(codec);
    enc_ctx_->width = enc_width_;
    enc_ctx_->height = enc_height_;
    enc_ctx_->bit_rate = bitrate_kbps_ * 1000;
    enc_ctx_->rc_max_rate = (bitrate_kbps_ * 1000) * 2;
    enc_ctx_->rc_buffer_size = (bitrate_kbps_ * 1000) / 20;
    enc_ctx_->time_base = {1, 20};
    enc_ctx_->framerate = {20, 1};
    enc_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
    enc_ctx_->gop_size = 20;
    enc_ctx_->max_b_frames = 0;
    av_opt_set(enc_ctx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(enc_ctx_->priv_data, "tune", "zerolatency", 0);
    av_opt_set(enc_ctx_->priv_data, "profile", "baseline", 0);
    av_opt_set(enc_ctx_->priv_data, "me", "dia", 0);
    av_opt_set(enc_ctx_->priv_data, "subq", "1", 0);
    av_opt_set(enc_ctx_->priv_data, "trellis", "0", 0);
    av_opt_set(enc_ctx_->priv_data, "no_dct_decimate", "1", 0);
    av_opt_set(enc_ctx_->priv_data, "fast_pskip", "1", 0);
    avcodec_open2(enc_ctx_, codec, nullptr);

    enc_frame_ = av_frame_alloc();
    enc_frame_->format = enc_ctx_->pix_fmt;
    enc_frame_->width = enc_width_;
    enc_frame_->height = enc_height_;
    av_frame_get_buffer(enc_frame_, 0);

    enc_pkt_ = av_packet_alloc();
    sws_ctx_ = sws_getContext(enc_width_, enc_height_, AV_PIX_FMT_BGR24,
                               enc_width_, enc_height_, AV_PIX_FMT_YUV420P,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    encoder_ready_ = true;
    RCLCPP_INFO(this->get_logger(), "编码器初始化完成 %dx%d H.264", enc_width_, enc_height_);
}

void CameraStreamer::cleanupEncoder()
{
    if (enc_pkt_) av_packet_free(&enc_pkt_);
    if (enc_frame_) av_frame_free(&enc_frame_);
    if (sws_ctx_) sws_freeContext(sws_ctx_);
    if (enc_ctx_) avcodec_free_context(&enc_ctx_);
}

bool CameraStreamer::encodeFrame(const cv::Mat &bgr, std::vector<uint8_t> &out, bool force_key)
{
    if (!encoder_ready_) return false;

    const int stride[1] = { static_cast<int>(bgr.step[0]) };
    sws_scale(sws_ctx_, &bgr.data, stride, 0, enc_height_,
              enc_frame_->data, enc_frame_->linesize);

    if (force_key) enc_frame_->pict_type = AV_PICTURE_TYPE_I;
    enc_frame_->pts = pts_++;

    int ret = avcodec_send_frame(enc_ctx_, enc_frame_);
    if (ret < 0) return false;

    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx_, enc_pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;
        out.insert(out.end(), enc_pkt_->data, enc_pkt_->data + enc_pkt_->size);
        av_packet_unref(enc_pkt_);
    }
    return !out.empty();
}

bool CameraStreamer::initTcpServer()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ == -1) return false;
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    for (int retry = 0; retry < 5; retry++) {
        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) == 0) {
            listen(server_fd_, 1);
            RCLCPP_INFO(this->get_logger(), "TCP server 监听端口 %d", port_);
            return true;
        }
        RCLCPP_WARN(this->get_logger(), "端口 %d 绑定失败，重试 %d/5...", port_, retry+1);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    RCLCPP_ERROR(this->get_logger(), "端口 %d 绑定失败", port_);
    return false;
}

void CameraStreamer::publishHeartbeat()
{
    auto msg = std_msgs::msg::Bool();
    msg.data = true;
    heartbeat_pub_->publish(msg);
}

void CameraStreamer::streamLoop()
{
    cv::Mat frame;
    std::vector<uint8_t> enc_buf;

        RCLCPP_INFO(this->get_logger(), "推流线程启动: target_fps=%d/%d, bitrate=%dkbps",
                    enc_ctx_->framerate.num, enc_ctx_->framerate.den, bitrate_kbps_);

        // 设置 accept 超时，让 Ctrl+C 能快速退出
        int timeout_ms = 1000; // 1秒超时
        setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

        while (running_ && rclcpp::ok()) {
            RCLCPP_INFO(this->get_logger(), "等待客户端连接...");
            client_fd_ = accept(server_fd_, nullptr, nullptr);
            if (client_fd_ == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // accept 超时，继续检查 running_
                    continue;
                }
                if (running_) std::this_thread::sleep_for(500ms);
                continue;
            }
            RCLCPP_INFO(this->get_logger(), "客户端已连接");

            bool first_frame = true;
            int frame_count = 0;
            int64_t last_frame_ts = 0;  // 记录最新帧的时间戳，检测相机源是否还活着
            int64_t last_log_ts = 0;    // 上次日志时间
            int encode_count = 0;       // 实际编码帧数（用于统计实际帧率）
            int64_t last_send_ts = 0;   // 上次发送时间戳
            int64_t encode_total_us = 0;  // 累计编码耗时（微秒）
            int64_t send_total_us = 0;    // 累计发送耗时（微秒）
            int64_t max_encode_us = 0;    // 单帧最大编码耗时
            int64_t max_send_us = 0;      // 单帧最大发送耗时
            size_t max_frame_size = 0;    // 单帧最大体积

            // 设置 recv/send 超时
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 500000; // 500ms
            setsockopt(client_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(client_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

            // 启用 TCP keepalive，让对端断开时能快速检测到
            int keepalive = 1;
            setsockopt(client_fd_, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            int keepidle = 3;     // 3 秒无数据后开始探测
            int keepintvl = 1;    // 探测间隔 1 秒
            int keepcnt = 3;      // 连续 3 次失败认为断开
            setsockopt(client_fd_, SOL_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
            setsockopt(client_fd_, SOL_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
            setsockopt(client_fd_, SOL_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

            while (running_ && rclcpp::ok()) {
                {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    if (latest_frame_.empty()) {
                        frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(255, 0, 0));
                    } else {
                        frame = latest_frame_.clone();
                    }
                }

                // 检测相机源是否还活着：如果 3 秒内没有收到新图像，主动断开让客户端重连
                int64_t now = this->now().nanoseconds();
                if (!latest_frame_.empty() && last_frame_ts > 0 &&
                    (now - last_frame_ts) > 3 * 1000000000LL) {
                    RCLCPP_WARN(this->get_logger(), "相机源超时（>3s 无新帧），主动断开");
                    break;
                }
                if (!latest_frame_.empty()) {
                    last_frame_ts = now;
                }

                enc_buf.clear();
                if (encoder_ready_) {
                    auto encode_start = std::chrono::steady_clock::now();

                    // 每 60 帧（约 3 秒）强制插入关键帧，减少大帧对帧率的冲击
                    bool force_key = first_frame || (++frame_count % 60 == 0);
                    if (!encodeFrame(frame, enc_buf, force_key)) {
                        std::this_thread::sleep_for(10ms);
                        continue;
                    }
                    first_frame = false;
                    encode_count++;

                    auto encode_end = std::chrono::steady_clock::now();
                    int64_t encode_us = std::chrono::duration_cast<std::chrono::microseconds>(
                        encode_end - encode_start).count();
                    encode_total_us += encode_us;
                    if (encode_us > max_encode_us) max_encode_us = encode_us;

                    // 日志：每 30 帧（~1.5 秒）打印一次，含单帧最大值
                    if (frame_count % 30 == 0) {
                        int64_t now_ms = this->now().nanoseconds() / 1000000;
                        double avg_encode_us = (double)encode_total_us / encode_count;
                        if (last_log_ts > 0) {
                            double elapsed_s = (now_ms - last_log_ts) / 1000.0;
                            double actual_fps = encode_count / elapsed_s;
                            double avg_send_us = (double)send_total_us / encode_count;
                            RCLCPP_INFO(this->get_logger(),
                                "推流: frame=%d, enc_avg=%.0fus, enc_max=%ldus, send_avg=%.0fus, send_max=%ldus, max_size=%zu, fps=%.1f",
                                frame_count, avg_encode_us, max_encode_us,
                                avg_send_us, max_send_us, max_frame_size, actual_fps);
                        } else {
                            RCLCPP_INFO(this->get_logger(),
                                "推流: frame=%d, enc_avg=%.0fus, enc_max=%ldus, max_size=%zu",
                                frame_count, avg_encode_us, max_encode_us, max_frame_size);
                        }
                        last_log_ts = now_ms;
                        encode_count = 0;
                        encode_total_us = 0;
                        send_total_us = 0;
                        max_encode_us = 0;
                        max_send_us = 0;
                        max_frame_size = 0;
                    }
                } else {
                    continue;
                }

                // 用网络序（大端）发送长度，与 Python 端 int.from_bytes(..., 'big') 一致
                auto send_start = std::chrono::steady_clock::now();
                uint32_t size_net = htonl((uint32_t)enc_buf.size());
                if (send(client_fd_, &size_net, 4, 0) <= 0 ||
                    send(client_fd_, enc_buf.data(), enc_buf.size(), 0) <= 0) {
                    RCLCPP_WARN(this->get_logger(), "客户端断开");
                    break;
                }
                auto send_end = std::chrono::steady_clock::now();
                int64_t send_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    send_end - send_start).count();
                send_total_us += send_us;
                if (send_us > max_send_us) max_send_us = send_us;
                if (enc_buf.size() > max_frame_size) max_frame_size = enc_buf.size();

                // 精确帧率控制：用时间戳而非固定 sleep
                // 目标帧率由 enc_ctx_->framerate 决定（当前 20fps → 每帧 50ms）
                int64_t target_interval_ns = 1000000000LL / 20;  // 50ms
                int64_t now_send = this->now().nanoseconds();
                if (last_send_ts > 0) {
                    int64_t elapsed = now_send - last_send_ts;
                    if (elapsed < target_interval_ns) {
                        std::this_thread::sleep_for(
                            std::chrono::nanoseconds(target_interval_ns - elapsed));
                    }
                }
                last_send_ts = this->now().nanoseconds();
            }
            if (client_fd_ > 0) { close(client_fd_); client_fd_ = -1; }
        }
    }

// 成员变量定义见 camera_streamer.h
