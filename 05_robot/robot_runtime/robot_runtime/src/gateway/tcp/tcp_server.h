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

// ============================================================================
// TcpServer — TCP 远程管控服务端
// ============================================================================
// 内嵌在 Runtime 进程中，接收远程客户端指令，转发到 CommandDispatcher。
// ============================================================================
struct TcpConfig {
    bool     enabled      = true;
    uint16_t port         = 9527;
    std::string bind_addr = "0.0.0.0";
    std::string auth_token;
    int      timeout_ms   = 5000;
    int      max_clients  = 4;
};

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
