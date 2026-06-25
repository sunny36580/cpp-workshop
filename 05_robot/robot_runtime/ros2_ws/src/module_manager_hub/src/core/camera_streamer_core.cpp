#include "module_manager_hub/core/camera_streamer_core.h"
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <sys/poll.h>
#include <cstring>
#include <cerrno>
#include <cstdio>

CameraStreamerCore::CameraStreamerCore()
{
}

CameraStreamerCore::~CameraStreamerCore()
{
    cleanupEncoder();
    if (server_fd_ > 0) { close(server_fd_); server_fd_ = -1; }
    if (client_fd_ > 0) { close(client_fd_); client_fd_ = -1; }
}

void CameraStreamerCore::logInfo(const std::string& msg)
{
    if (log_cb_) log_cb_(0, msg);
}

void CameraStreamerCore::logWarn(const std::string& msg)
{
    if (log_cb_) log_cb_(1, msg);
}

void CameraStreamerCore::logError(const std::string& msg)
{
    if (log_cb_) log_cb_(2, msg);
}

// =====================================================================
// 编码器
// =====================================================================
bool CameraStreamerCore::initEncoder(int width, int height, int bitrate_kbps)
{
    cleanupEncoder();
    enc_width_ = width;
    enc_height_ = height;
    bitrate_kbps_ = bitrate_kbps;

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) { logError("找不到 H.264 编码器"); return false; }

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
    pts_ = 0;

    char buf[128];
    snprintf(buf, sizeof(buf), "编码器初始化完成 %dx%d H.264", enc_width_, enc_height_);
    logInfo(buf);
    return true;
}

void CameraStreamerCore::cleanupEncoder()
{
    if (enc_pkt_) av_packet_free(&enc_pkt_);
    if (enc_frame_) av_frame_free(&enc_frame_);
    if (sws_ctx_) sws_freeContext(sws_ctx_);
    if (enc_ctx_) avcodec_free_context(&enc_ctx_);
    encoder_ready_ = false;
}

bool CameraStreamerCore::encodeFrame(const cv::Mat &bgr, std::vector<uint8_t> &out, bool force_key)
{
    if (!encoder_ready_) return false;

    const int stride[1] = { static_cast<int>(bgr.step[0]) };
    sws_scale(sws_ctx_, &bgr.data, stride, 0, enc_height_,
              enc_frame_->data, enc_frame_->linesize);

    if (force_key) enc_frame_->pict_type = AV_PICTURE_TYPE_I;
    enc_frame_->pts = pts_++;

    int ret = avcodec_send_frame(enc_ctx_, enc_frame_);
    if (ret < 0) return false;

    out.clear();
    while (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx_, enc_pkt_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) return false;
        out.insert(out.end(), enc_pkt_->data, enc_pkt_->data + enc_pkt_->size);
        av_packet_unref(enc_pkt_);
    }
    return !out.empty();
}

// TCP Server
bool CameraStreamerCore::initTcpServer(int port)
{
    port_ = port;
    if (server_fd_ > 0) { close(server_fd_); server_fd_ = -1; }

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
            char buf[128];
            snprintf(buf, sizeof(buf), "TCP server 监听端口 %d", port_);
            logInfo(buf);
            return true;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "端口 %d 绑定失败，重试 %d/5...", port_, retry+1);
        logWarn(buf);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "端口 %d 绑定失败", port_);
    logError(buf);
    return false;
}

// 帧缓存
void CameraStreamerCore::setFrame(const cv::Mat& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    latest_frame_ = frame.clone();
}

bool CameraStreamerCore::getFrame(cv::Mat& frame)
{
    std::lock_guard<std::mutex> lock(frame_mutex_);
    if (latest_frame_.empty()) return false;
    frame = latest_frame_.clone();
    return true;
}

// 推流主循环
void CameraStreamerCore::streamLoop(std::atomic<bool>& running,
                                     std::function<bool()> ok_check)
{
    logInfo("推流线程启动");

    // 设置 accept 超时，让退出信号能快速响应
    int timeout_ms = 1000;
    setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));

    while (running && (!ok_check || ok_check())) {
        logInfo("等待客户端连接...");
        client_fd_ = accept(server_fd_, nullptr, nullptr);
        if (client_fd_ == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            logError("accept 失败");
            break;
        }

        logInfo("客户端已连接");

        // 设置发送超时
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        setsockopt(client_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        // 禁用 Nagle 算法
        int flag = 1;
        setsockopt(client_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        uint64_t frame_idx = 0;
        while (running && (!ok_check || ok_check())) {
            cv::Mat frame;
            if (!getFrame(frame)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            std::vector<uint8_t> enc_buf;
            if (!encodeFrame(frame, enc_buf, (frame_idx % 60 == 0))) {
                continue;
            }

            // 发送: [帧长度:4B][H264数据]
            uint32_t frame_len = htonl(static_cast<uint32_t>(enc_buf.size()));
            ssize_t sent = send(client_fd_, &frame_len, sizeof(frame_len), 0);
            if (sent <= 0) {
                logWarn("发送帧长度失败，客户端断开");
                break;
            }

            size_t total_sent = 0;
            while (total_sent < enc_buf.size()) {
                sent = send(client_fd_, enc_buf.data() + total_sent,
                            enc_buf.size() - total_sent, 0);
                if (sent <= 0) {
                    logWarn("发送帧数据失败，客户端断开");
                    goto disconnect;
                }
                total_sent += sent;
            }

            frame_idx++;
        }

disconnect:
        if (client_fd_ > 0) {
            close(client_fd_);
            client_fd_ = -1;
        }
        logInfo("客户端断开");
    }
}
