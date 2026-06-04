#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

using namespace std::chrono_literals;

class CameraStreamer : public rclcpp::Node
{
public:
    explicit CameraStreamer(const std::string& node_name)
    : Node(node_name)
    {
        this->declare_parameter<std::string>("image_topic", "/camera/image_raw");
        this->declare_parameter<int>("port", 8888);

        image_topic_ = this->get_parameter("image_topic").as_string();
        port_        = this->get_parameter("port").as_int();

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

    ~CameraStreamer() override
    {
        running_ = false;
        if (server_fd_ > 0) { close(server_fd_); server_fd_ = -1; }
        if (stream_thread_.joinable()) stream_thread_.join();
        if (client_fd_ > 0) { close(client_fd_); client_fd_ = -1; }
        cleanupEncoder();
    }

private:
    void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg)
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

    void initEncoder()
    {
        const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) { RCLCPP_ERROR(this->get_logger(), "找不到 H.264 编码器"); return; }
        enc_ctx_ = avcodec_alloc_context3(codec);
        enc_ctx_->width = enc_width_;
        enc_ctx_->height = enc_height_;
        enc_ctx_->time_base = {1, 30};
        enc_ctx_->framerate = {30, 1};
        enc_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        enc_ctx_->gop_size = 30;
        enc_ctx_->max_b_frames = 0;
        av_opt_set(enc_ctx_->priv_data, "preset", "ultrafast", 0);
        av_opt_set(enc_ctx_->priv_data, "tune", "zerolatency", 0);
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

    void cleanupEncoder()
    {
        if (enc_pkt_) av_packet_free(&enc_pkt_);
        if (enc_frame_) av_frame_free(&enc_frame_);
        if (sws_ctx_) sws_freeContext(sws_ctx_);
        if (enc_ctx_) avcodec_free_context(&enc_ctx_);
    }

    bool encodeFrame(const cv::Mat &bgr, std::vector<uint8_t> &out, bool force_key)
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

    bool initTcpServer()
    {
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ == -1) return false;
        int opt = 1;
        setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

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

    void publishHeartbeat()
    {
        auto msg = std_msgs::msg::Bool();
        msg.data = true;
        heartbeat_pub_->publish(msg);
    }

    void streamLoop()
    {
        cv::Mat frame;
        std::vector<uint8_t> enc_buf;

        while (running_ && rclcpp::ok()) {
            RCLCPP_INFO(this->get_logger(), "等待客户端连接...");
            client_fd_ = accept(server_fd_, nullptr, nullptr);
            if (client_fd_ == -1) {
                if (running_) std::this_thread::sleep_for(1s);
                continue;
            }
            RCLCPP_INFO(this->get_logger(), "客户端已连接");

            bool first_frame = true;

            while (running_ && rclcpp::ok()) {
                {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    if (latest_frame_.empty()) {
                        frame = cv::Mat(480, 640, CV_8UC3, cv::Scalar(255, 0, 0));
                    } else {
                        frame = latest_frame_.clone();
                    }
                }

                enc_buf.clear();
                if (encoder_ready_) {
                    if (!encodeFrame(frame, enc_buf, first_frame)) {
                        std::this_thread::sleep_for(10ms);
                        continue;
                    }
                    first_frame = false;
                } else {
                    continue;
                }

                int size = (int)enc_buf.size();
                if (send(client_fd_, &size, 4, 0) <= 0 ||
                    send(client_fd_, enc_buf.data(), size, 0) <= 0) {
                    RCLCPP_WARN(this->get_logger(), "客户端断开");
                    break;
                }
                std::this_thread::sleep_for(10ms);
            }
            if (client_fd_ > 0) { close(client_fd_); client_fd_ = -1; }
        }
    }

    std::string image_topic_;
    int port_;
    int server_fd_ = -1, client_fd_ = -1;
    std::thread stream_thread_;
    std::atomic<bool> running_ = true;

    cv::Mat latest_frame_;
    std::mutex frame_mutex_;

    bool encoder_ready_ = false;
    int enc_width_ = 0, enc_height_ = 0;
    int64_t pts_ = 0;
    AVCodecContext *enc_ctx_ = nullptr;
    AVFrame *enc_frame_ = nullptr;
    AVPacket *enc_pkt_ = nullptr;
    SwsContext *sws_ctx_ = nullptr;

    rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr heartbeat_pub_;
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CameraStreamer>("camera_streamer_node"));
    rclcpp::shutdown();
    return 0;
}
