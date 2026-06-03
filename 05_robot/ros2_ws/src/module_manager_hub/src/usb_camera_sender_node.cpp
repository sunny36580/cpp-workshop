#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
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
        this->declare_parameter<int>("jpeg_quality", 75);
        this->declare_parameter<int>("port", 8888);
        this->declare_parameter<bool>("use_h264", true);

        image_topic_   = this->get_parameter("image_topic").as_string();
        jpeg_quality_  = this->get_parameter("jpeg_quality").as_int();
        port_          = this->get_parameter("port").as_int();
        use_h264_      = this->get_parameter("use_h264").as_bool();

        heartbeat_pub_ = this->create_publisher<std_msgs::msg::Bool>("/camera/status", 10);
        heartbeat_timer_ = this->create_wall_timer(
            500ms, std::bind(&CameraStreamer::publishHeartbeat, this));

        image_sub_ = this->create_subscription<sensor_msgs::msg::Image>(
            image_topic_, rclcpp::SensorDataQoS(),
            std::bind(&CameraStreamer::imageCallback, this, std::placeholders::_1));

        if (!initTcpServer()) {
            RCLCPP_ERROR(this->get_logger(), "❌ TCP服务器初始化失败");
            rclcpp::shutdown();
            return;
        }

        RCLCPP_INFO(this->get_logger(), "✅ 图像推流节点启动  topic=%s  codec=%s  port=%d",
                    image_topic_.c_str(), use_h264_ ? "H.264" : "MJPEG", port_);
        stream_thread_ = std::thread(&CameraStreamer::streamLoop, this);
    }

    ~CameraStreamer() override
    {
        running_ = false;
        if (stream_thread_.joinable()) stream_thread_.join();
        if (client_fd_ > 0) close(client_fd_);
        if (server_fd_ > 0) close(server_fd_);
        cleanupEncoder();
    }

private:
    void imageCallback(const sensor_msgs::msg::Image::SharedPtr msg)
    {
        try {
            auto cv_ptr = cv_bridge::toCvCopy(msg, "bgr8");
            std::lock_guard<std::mutex> lock(frame_mutex_);
            latest_frame_ = cv_ptr->image.clone();
            if (!encoder_ready_) {
                enc_width_ = latest_frame_.cols;
                enc_height_ = latest_frame_.rows;
                initEncoder();
            }
        } catch (cv_bridge::Exception &e) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                "cv_bridge failed: %s", e.what());
        }
    }

    void initEncoder()
    {
        const AVCodec *codec = avcodec_find_encoder(
            use_h264_ ? AV_CODEC_ID_H264 : AV_CODEC_ID_MJPEG);
        if (!codec) { RCLCPP_ERROR(this->get_logger(), "找不到编码器"); return; }
        enc_ctx_ = avcodec_alloc_context3(codec);
        enc_ctx_->width = enc_width_;
        enc_ctx_->height = enc_height_;
        enc_ctx_->time_base = {1, 30};
        enc_ctx_->framerate = {30, 1};
        enc_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
        if (use_h264_) {
            enc_ctx_->gop_size = 30;
            enc_ctx_->max_b_frames = 0;
            av_opt_set(enc_ctx_->priv_data, "preset", "ultrafast", 0);
            av_opt_set(enc_ctx_->priv_data, "tune", "zerolatency", 0);
        }
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
        RCLCPP_INFO(this->get_logger(), "编码器初始化完成 %dx%d %s",
                    enc_width_, enc_height_, use_h264_ ? "H.264" : "MJPEG");
    }

    void cleanupEncoder()
    {
        if (enc_pkt_) av_packet_free(&enc_pkt_);
        if (enc_frame_) av_frame_free(&enc_frame_);
        if (sws_ctx_) sws_freeContext(sws_ctx_);
        if (enc_ctx_) avcodec_free_context(&enc_ctx_);
    }

    bool encodeFrame(const cv::Mat &bgr, std::vector<uint8_t> &out)
    {
        if (!encoder_ready_) return false;

        // BGR → YUV420P
        const int stride = enc_width_ * 3;
        sws_scale(sws_ctx_, &bgr.data, &stride, 0, enc_height_,
                  enc_frame_->data, enc_frame_->linesize);

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
        if (bind(server_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
            RCLCPP_ERROR(this->get_logger(), "端口 %d 绑定失败", port_);
            return false;
        }
        listen(server_fd_, 1);
        return true;
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
        std::vector<uchar> enc_buf;
        std::vector<int> jpg_params = {cv::IMWRITE_JPEG_QUALITY, jpeg_quality_};

        while (running_ && rclcpp::ok()) {
            RCLCPP_INFO(this->get_logger(), "等待客户端连接...");
            client_fd_ = accept(server_fd_, nullptr, nullptr);
            if (client_fd_ == -1) {
                if (running_) std::this_thread::sleep_for(1s);
                continue;
            }
            RCLCPP_INFO(this->get_logger(), "客户端已连接，开始推流 (%s)",
                        use_h264_ ? "H.264" : "MJPEG");

            while (running_ && rclcpp::ok()) {
                {
                    std::lock_guard<std::mutex> lock(frame_mutex_);
                    if (latest_frame_.empty()) {
                        std::this_thread::sleep_for(10ms);
                        continue;
                    }
                    frame = latest_frame_.clone();
                }

                enc_buf.clear();
                if (use_h264_ && encoder_ready_) {
                    if (!encodeFrame(frame, enc_buf)) continue;
                } else {
                    // MJPEG fallback
                    cv::imencode(".jpg", frame, enc_buf, jpg_params);
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

    // parameters
    std::string image_topic_;
    int jpeg_quality_, port_;
    bool use_h264_;
    int server_fd_ = -1, client_fd_ = -1;
    std::thread stream_thread_;
    std::atomic<bool> running_ = true;

    // frame buffer
    cv::Mat latest_frame_;
    std::mutex frame_mutex_;

    // H.264 encoder
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
