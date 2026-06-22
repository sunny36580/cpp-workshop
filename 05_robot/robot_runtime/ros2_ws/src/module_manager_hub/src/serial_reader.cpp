#include "module_manager_hub/serial_reader.h"
#include <cstdio>

// =====================================================================
// 构造 & 析构
// =====================================================================
SerialReader::SerialReader()
    : serial_port_(io_context_)
{
}

SerialReader::~SerialReader()
{
    close();
}

// =====================================================================
// 打开 / 关闭
// =====================================================================
bool SerialReader::open(const std::string& port, int baud_rate)
{
    try {
        serial_port_.open(port);
        serial_port_.set_option(boost::asio::serial_port::baud_rate(baud_rate));
        serial_port_.set_option(boost::asio::serial_port::flow_control(
            boost::asio::serial_port::flow_control::none));
        serial_port_.set_option(boost::asio::serial_port::parity(
            boost::asio::serial_port::parity::none));
        serial_port_.set_option(boost::asio::serial_port::stop_bits(
            boost::asio::serial_port::stop_bits::one));
        serial_port_.set_option(boost::asio::serial_port::character_size(8));

        running_ = true;
        doRead();
        io_thread_ = std::thread([this]() { io_context_.run(); });

        printf("[SerialReader] 串口已打开: %s @ %d baud\n", port.c_str(), baud_rate);
        return true;
    } catch (std::exception& e) {
        fprintf(stderr, "[SerialReader] 串口打开失败 %s: %s\n", port.c_str(), e.what());
        return false;
    }
}

void SerialReader::close()
{
    running_ = false;
    if (serial_port_.is_open()) {
        boost::system::error_code ec;
        serial_port_.close(ec);
    }
    io_context_.stop();
    if (io_thread_.joinable()) io_thread_.join();
}

bool SerialReader::is_open() const
{
    return serial_port_.is_open();
}

// =====================================================================
// 异步读取
// =====================================================================
void SerialReader::doRead()
{
    serial_port_.async_read_some(boost::asio::buffer(rx_buf_),
        [this](boost::system::error_code ec, std::size_t bytes) {
            if (ec) {
                if (running_) {
                    fprintf(stderr, "[SerialReader] 读错误: %s\n", ec.message().c_str());
                    doRead();
                }
                return;
            }
            if (data_cb_) {
                data_cb_(rx_buf_.data(), bytes);
            }
            doRead();
        });
}

// =====================================================================
// 写
// =====================================================================
void SerialReader::write(const uint8_t* data, size_t len)
{
    boost::system::error_code ec;
    boost::asio::write(serial_port_, boost::asio::buffer(data, len), ec);
    if (ec) {
        fprintf(stderr, "[SerialReader] 同步写失败: %s\n", ec.message().c_str());
    }
}

void SerialReader::writeAsync(const uint8_t* data, size_t len)
{
    auto buf = std::make_shared<std::vector<uint8_t>>(data, data + len);
    boost::asio::async_write(serial_port_, boost::asio::buffer(*buf),
        [this, buf](boost::system::error_code ec, std::size_t /*bytes*/) {
            if (ec) {
                fprintf(stderr, "[SerialReader] 异步写失败: %s\n", ec.message().c_str());
            }
        });
}
