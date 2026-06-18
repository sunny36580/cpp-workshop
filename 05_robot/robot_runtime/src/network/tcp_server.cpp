#include "core/runtime.h"
#include "network/tcp_server.h"
#include "network/command_dispatcher.h"

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <vector>

namespace robot_runtime::net {

static constexpr size_t READ_BUF_SIZE = 8192;

TcpServer::TcpServer(Runtime& runtime, TcpConfig config)
    : runtime_(runtime)
    , config_(std::move(config))
    , auth_(config_.auth_token)
{
}

TcpServer::~TcpServer() {
    stop();
}

// ---------------------------------------------------------------------------
bool TcpServer::start() {
    if (running_.exchange(true)) return true;

    // 创建 socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listen_fd_ < 0) {
        perror("[TcpServer] socket");
        running_ = false;
        return false;
    }

    // 地址复用
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(config_.port);
    addr.sin_addr.s_addr = config_.bind_addr == "0.0.0.0"
                               ? INADDR_ANY
                               : inet_addr(config_.bind_addr.c_str());

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "[TcpServer] bind port %u: %s\n",
                config_.port, strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }

    if (listen(listen_fd_, config_.max_clients) < 0) {
        perror("[TcpServer] listen");
        close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return false;
    }

    printf("[TcpServer] listening on %s:%u\n",
           config_.bind_addr.c_str(), config_.port);

    // 启动接受线程
    acceptor_thread_ = std::thread(&TcpServer::acceptor_loop, this);
    return true;
}

// ---------------------------------------------------------------------------
void TcpServer::stop() {
    if (!running_.exchange(false)) return;

    // 关闭监听 socket，中断 accept
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    if (acceptor_thread_.joinable()) {
        acceptor_thread_.join();
    }

    // 等待所有客户端线程结束
    std::lock_guard<std::mutex> lock(threads_mutex_);
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    client_threads_.clear();

    printf("[TcpServer] stopped\n");
}

// ---------------------------------------------------------------------------
void TcpServer::serve() {
    if (!start()) return;
    printf("[TcpServer] serving forever (Ctrl+C to stop)\n");

    // 简单地等待停止信号
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ---------------------------------------------------------------------------
void TcpServer::acceptor_loop() {
    CommandDispatcher dispatcher(runtime_);

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        int client_fd = accept4(listen_fd_, (struct sockaddr*)&client_addr,
                                &addr_len, SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (running_) {
                fprintf(stderr, "[TcpServer] accept: %s\n", strerror(errno));
            }
            continue;
        }

        char client_ip[64];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[TcpServer] client connected: %s:%d (fd=%d)\n",
               client_ip, ntohs(client_addr.sin_port), client_fd);

        // 每个客户端一个线程处理
        std::lock_guard<std::mutex> lock(threads_mutex_);
        client_threads_.emplace_back(&TcpServer::client_session,
                                      this, client_fd);
    }
}

// ---------------------------------------------------------------------------
void TcpServer::client_session(int client_fd) {
    CommandDispatcher dispatcher(runtime_);
    std::vector<uint8_t> read_buf(READ_BUF_SIZE);
    std::vector<uint8_t> accum;  // 粘包累积缓冲区
    bool authenticated = !auth_.enabled();

    while (running_) {
        // poll 等待数据，支持超时
        struct pollfd pfd;
        pfd.fd     = client_fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 1000);  // 1s 超时，用于检测 running_ 变化
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) continue;  // 超时，检查 running_ 后重试

        if (!(pfd.revents & POLLIN)) break;

        ssize_t n = read(client_fd, read_buf.data(), read_buf.size());
        if (n <= 0) break;  // 断开或错误

        // 粘包处理：追加到累积缓冲区
        accum.insert(accum.end(), read_buf.begin(), read_buf.begin() + n);

        // 尝试解析完整帧
        while (true) {
            RequestPacket req;
            int result = parse_frame(accum.data(), accum.size(), req);
            if (result > 0) {
                // 解析成功，移除已消费的字节
                accum.erase(accum.begin(), accum.begin() + result);

                // 鉴权检查
                if (!authenticated) {
                    if (req.cmd == CMD_AUTH) {
                        std::string token(to_string(req.data));
                        authenticated = auth_.authenticate(token);
                        ResponsePacket auth_resp;
                        auth_resp.version = req.version;
                        auth_resp.cmd     = req.cmd;
                        auth_resp.code    = authenticated ? RESP_OK : RESP_DENIED;
                        auth_resp.data    = authenticated
                                                ? to_bytes("auth ok")
                                                : to_bytes("auth denied");
                        auto frame = pack_response(auth_resp);
                        auto wlen = write(client_fd, frame.data(), frame.size());
                        (void)wlen;
                        if (!authenticated) {
                            fprintf(stderr, "[TcpServer] auth failed, closing\n");
                            close(client_fd);
                            return;
                        }
                        continue;
                    } else {
                        // 未认证，拒绝
                        ResponsePacket deny;
                        deny.version = req.version;
                        deny.cmd     = req.cmd;
                        deny.code    = RESP_DENIED;
                        deny.data    = to_bytes("auth required");
                        auto frame = pack_response(deny);
                        auto wlen = write(client_fd, frame.data(), frame.size());
                        (void)wlen;
                        continue;
                    }
                }

                // 分发指令
                ResponsePacket resp = dispatcher.dispatch(req);
                auto frame = pack_response(resp);
                auto wlen = write(client_fd, frame.data(), frame.size());
                (void)wlen;

            } else if (result == 0) {
                // 需要更多数据
                break;
            } else {
                // 格式错误，丢弃损坏数据
                int skip = -result;
                if (skip <= 0) skip = 1;
                if (skip > (int)accum.size()) skip = (int)accum.size();
                accum.erase(accum.begin(), accum.begin() + skip);
                fprintf(stderr, "[TcpServer] frame parse error, skipped %d bytes\n", skip);
            }
        }
    }

    close(client_fd);
    printf("[TcpServer] client disconnected (fd=%d)\n", client_fd);
}

} // namespace robot_runtime::net
