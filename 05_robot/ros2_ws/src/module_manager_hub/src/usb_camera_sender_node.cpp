#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/bool.hpp>
#include <opencv2/opencv.hpp>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <thread>
#include <atomic>

using namespace cv;
using namespace std::chrono_literals;

class UsbCameraSender : public rclcpp::Node
{
public:
    explicit UsbCameraSender(const std::string& node_name)
    : Node(node_name)
    {
        // 声明参数（可通过yaml配置覆盖默认值）
        this->declare_parameter<int>("device_id", 0);
        this->declare_parameter<int>("width", 1280);
        this->declare_parameter<int>("height", 720);
        this->declare_parameter<int>("fps", 30);
        this->declare_parameter<int>("jpeg_quality", 75);
        this->declare_parameter<int>("port", 8888);

        device_id_    = this->get_parameter("device_id").as_int();
        width_        = this->get_parameter("width").as_int();
        height_       = this->get_parameter("height").as_int();
        fps_          = this->get_parameter("fps").as_int();
        jpeg_quality_ = this->get_parameter("jpeg_quality").as_int();
        port_         = this->get_parameter("port").as_int();

        // 心跳发布（给模块管理器监控用）
        heartbeat_pub_ = this->create_publisher<std_msgs::msg::Bool>("/camera/status", 10);
        heartbeat_timer_ = this->create_wall_timer(
            500ms, std::bind(&UsbCameraSender::publishHeartbeat, this));

        // 初始化相机
        if (!initCamera()) {
            RCLCPP_ERROR(this->get_logger(), "❌ 相机初始化失败");
            rclcpp::shutdown();
            return;
        }

        // 初始化TCP服务器
        if (!initTcpServer()) {
            RCLCPP_ERROR(this->get_logger(), "❌ TCP服务器初始化失败");
            rclcpp::shutdown();
            return;
        }

        RCLCPP_INFO(this->get_logger(), "✅ USB相机推流节点启动");
        RCLCPP_INFO(this->get_logger(), "   设备: /dev/video%d", device_id_);
        RCLCPP_INFO(this->get_logger(), "   分辨率: %dx%d@%dfps", width_, height_, fps_);
        RCLCPP_INFO(this->get_logger(), "   JPEG质量: %d%%", jpeg_quality_);
        RCLCPP_INFO(this->get_logger(), "   TCP端口: %d", port_);
        RCLCPP_INFO(this->get_logger(), "   等待客户端连接...");

        // 启动推流线程
        stream_thread_ = std::thread(&UsbCameraSender::streamLoop, this);
    }

    ~UsbCameraSender() override
    {
        running_ = false;
        if (stream_thread_.joinable()) stream_thread_.join();
        if (client_fd_ > 0) close(client_fd_);
        if (server_fd_ > 0) close(server_fd_);
        cap_.release();
        RCLCPP_INFO(this->get_logger(), "❌ 相机推流节点已停止");
    }

private:
    bool initCamera()
    {
        cap_.open(device_id_);
        if (!cap_.isOpened()) {
            RCLCPP_ERROR(this->get_logger(), "无法打开 /dev/video%d", device_id_);
            return false;
        }
        cap_.set(CAP_PROP_FRAME_WIDTH, width_);
        cap_.set(CAP_PROP_FRAME_HEIGHT, height_);
        cap_.set(CAP_PROP_FPS, fps_);

        jpeg_params_ = {IMWRITE_JPEG_QUALITY, jpeg_quality_};
        return true;
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
        std_msgs::msg::Bool msg;
        msg.data = true;
        heartbeat_pub_->publish(msg);
    }

    void streamLoop()
    {
        Mat frame;
        std::vector<uchar> jpg_buf;

        while (running_ && rclcpp::ok()) {
            // 等待客户端连接
            RCLCPP_INFO(this->get_logger(), "等待客户端...");
            client_fd_ = accept(server_fd_, nullptr, nullptr);
            if (client_fd_ == -1) {
                if (running_) std::this_thread::sleep_for(1s);
                continue;
            }
            RCLCPP_INFO(this->get_logger(), "✅ 客户端已连接，开始推流");

            while (running_ && rclcpp::ok()) {
                cap_ >> frame;
                if (frame.empty()) {
                    std::this_thread::sleep_for(10ms);
                    continue;
                }

                // JPEG压缩
                imencode(".jpg", frame, jpg_buf, jpeg_params_);
                int size = (int)jpg_buf.size();

                // 发4字节长度头 + JPEG数据
                if (send(client_fd_, &size, 4, 0) <= 0 ||
                    send(client_fd_, jpg_buf.data(), size, 0) <= 0) {
                    RCLCPP_WARN(this->get_logger(), "客户端断开");
                    break;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            if (client_fd_ > 0) {
                close(client_fd_);
                client_fd_ = -1;
            }
        }
    }

    // 参数
    int device_id_, width_, height_, fps_, jpeg_quality_, port_;
    VideoCapture cap_;
    std::vector<int> jpeg_params_;

    int server_fd_ = -1, client_fd_ = -1;
    std::thread stream_thread_;
    std::atomic<bool> running_ = true;

    rclcpp::Publisher<std_msgs::msg::Bool>::SharedPtr heartbeat_pub_;
    rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

int main(int argc, char* argv[])
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<UsbCameraSender>("usb_camera_sender_node");
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
