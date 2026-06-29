#pragma once

#include <string>
#include <cstring>

namespace robot_runtime::net {

// ============================================================================
// AuthManager — TCP 连接鉴权
// ============================================================================
// 简单 token 认证。客户端连接后先发 CMD_AUTH 携带 token，
// 认证通过后才能执行其他指令。token 为空时跳过认证。
// ============================================================================
class AuthManager {
public:
    explicit AuthManager(std::string token = "")
        : token_(std::move(token)) {}

    bool enabled() const { return !token_.empty(); }

    bool authenticate(const std::string& client_token) const {
        if (!enabled()) return true;
        return token_ == client_token;
    }

    void set_token(const std::string& token) { token_ = token; }

private:
    std::string token_;
};

} // namespace robot_runtime::net
