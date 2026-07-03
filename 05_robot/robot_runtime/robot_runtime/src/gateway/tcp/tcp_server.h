/**
 * @file tcp_server.h
 * @brief TCP 远程管控服务端
 * @role gateway/tcp
 */
#pragma once

#include "gateway/tcp/protocol_parser.h"
#include "gateway/tcp/auth_manager.h"

#include <string>
#include <atomic>
#include <thread>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>

namespace robot_runtime {
class Runtime;
} // namespace robot_runtime

namespace robot_runtime::net {


/**
 * @class TcpConfig
 * @brief TCP 服务配置
 * @responsibility 配置 TCP 服务端的监听地址、端口、鉴权 token 等参数
 */
struct TcpConfig {
    bool     enabled      = true;
    uint16_t port         = 9527;
    std::string bind_addr = "0.0.0.0";
    std::string auth_token;
    int      timeout_ms   = 5000;
    int      max_clients  = 4;
};

/**
 * @class TcpServer
 * @brief TCP 远程管控服务端
 * @responsibility 管理 TCP 连接，处理客户端请求
 */

class TcpServer {
public:
    explicit TcpServer(Runtime& runtime, TcpConfig config = {});
    ~TcpServer();

    // 启动（在后台线程运行）
    bool start();

    // 停止
    void stop();

    // 阻塞运行（前台，直到 stop 被调用）
    void serve();

    bool is_running() const { return running_; }
    uint16_t port() const { return config_.port; }

private:
    void acceptor_loop();
    void client_session(int client_fd);

    Runtime& runtime_;
    TcpConfig config_;
    AuthManager auth_;
    std::atomic<bool> running_{false};

    int listen_fd_ = -1;
    std::thread acceptor_thread_;
    std::vector<std::thread> client_threads_;
    std::mutex threads_mutex_;
};

} // namespace robot_runtime::net
