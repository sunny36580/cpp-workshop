/**
 * @file service_client.h
 * @brief 服务调用客户端接口
 * @role runtime/service_access
 */
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>
#include <chrono>

namespace robot_runtime {

/** @brief 服务调用结果 */
struct ServiceResult {
    bool success = false;
    std::string error_msg;
    std::vector<uint8_t> payload;  // 响应数据

    static ServiceResult Ok() { return {true, "", {}}; }
    static ServiceResult Error(std::string msg) { return {false, std::move(msg), {}}; }
};

/// 服务客户端接口
/// 封装对单个外部服务的调用，通过 adapter 层下发。
/// 每个 ServiceClient 绑定一个具体的服务实例和通信协议。
class ServiceClient {
public:
    virtual ~ServiceClient() = default;

    /// 服务名称
    virtual const std::string& service_name() const = 0;

    /// 是否已连接
    virtual bool connected() const = 0;

    /// 发送请求并等待响应（同步）
    virtual ServiceResult call(const std::string& method,
                               const std::vector<uint8_t>& request,
                               std::chrono::milliseconds timeout = std::chrono::seconds(5)) = 0;

    /// 发送单向消息（无需响应）
    virtual bool send(const std::string& method,
                      const std::vector<uint8_t>& data) = 0;

    /// 注册异步回调（订阅）
    using ResponseCallback = std::function<void(const std::string& method,
                                                 const std::vector<uint8_t>& data)>;
    virtual void set_response_callback(ResponseCallback cb) = 0;

    /// 关闭连接
    virtual void close() = 0;
};

} // namespace robot_runtime
