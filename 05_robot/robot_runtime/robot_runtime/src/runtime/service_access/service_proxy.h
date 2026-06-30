#pragma once

#include <string>
#include <memory>
#include <functional>

#include "runtime/service_access/service_client.h"

namespace robot_runtime {

/// 服务代理
/// 对 ServiceClient 的封装，提供类型安全的接口。
/// runtime 上层通过 ServiceProxy 调用外部服务，不直接感知通信协议。
/// adapter 层的具体实现（ROS/TCP/AimRT）负责协议转换。
class ServiceProxy {
public:
    using StateCallback = std::function<void(const std::string& service, bool online)>;

    explicit ServiceProxy(std::string service_name,
                          std::unique_ptr<ServiceClient> client)
        : service_name_(std::move(service_name))
        , client_(std::move(client)) {}

    virtual ~ServiceProxy() = default;

    /// 服务名称
    const std::string& service_name() const { return service_name_; }

    /// 是否在线
    bool connected() const { return client_ && client_->connected(); }

    /// 发送心跳上报（由 monitor 驱动）
    /// 对应 heartbeat_collector 的心跳文件写入逻辑
    virtual bool report_heartbeat(const std::vector<uint8_t>& hb_data) {
        if (!client_) return false;
        return client_->send("heartbeat", hb_data);
    }

    /// 发送指令（对应 module_manager 的指令下发）
    virtual ServiceResult send_command(const std::string& cmd,
                                       const std::vector<uint8_t>& args,
                                       std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        if (!client_) return {false, "no client"};
        return client_->call(cmd, args, timeout);
    }

    /// 获取服务状态
    virtual ServiceResult query_status(std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
        if (!client_) return {false, "no client"};
        return client_->call("status", {}, timeout);
    }

    /// 设置连接状态变化回调
    void set_state_callback(StateCallback cb) { state_cb_ = std::move(cb); }

    /// 关闭代理
    void close() {
        if (client_) client_->close();
    }

protected:
    std::string service_name_;
    std::unique_ptr<ServiceClient> client_;
    StateCallback state_cb_;
};

} // namespace robot_runtime
