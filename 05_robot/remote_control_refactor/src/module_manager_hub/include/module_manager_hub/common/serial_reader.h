#pragma once

#include <boost/asio.hpp>
#include <array>
#include <functional>
#include <string>
#include <thread>
#include <cstdint>

/// 串口读取器 —— 只负责原始字节的收发，不涉及任何协议解析
/// 通过 DataCallback 将裸字节吐给上层，由各 node 自行完成帧同步/校验
class SerialReader {
public:
    using DataCallback = std::function<void(const uint8_t* data, size_t len)>;

    SerialReader();
    ~SerialReader();

    /// 打开串口并启动异步读取线程
    bool open(const std::string& port, int baud_rate);

    /// 关闭串口，停止 IO 线程
    void close();
    bool is_open() const;

    /// 设置裸数据回调（收到原始字节时触发）
    void setDataCallback(DataCallback cb) { data_cb_ = std::move(cb); }

    /// 同步写
    void write(const uint8_t* data, size_t len);

    /// 异步写（拷贝 frame，线程安全）
    void writeAsync(const uint8_t* data, size_t len);

private:
    void doRead();

    boost::asio::io_context io_context_;
    boost::asio::serial_port serial_port_;
    std::thread io_thread_;
    bool running_ = false;

    std::array<uint8_t, 256> rx_buf_;
    DataCallback data_cb_;
};
