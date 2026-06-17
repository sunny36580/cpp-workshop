#pragma once

#include <string>
#include <vector>

namespace robot_runtime {

// ============================================================================
// 通用错误码
// ============================================================================
enum class ErrorCode {
    OK = 0,
    FAILED = -1,
    NOT_FOUND = -2,
    ALREADY_RUNNING = -3,
    NOT_RUNNING = -4,
    TIMEOUT = -5,
    DEPENDENCY_FAILED = -6,
    INVALID_CONFIG = -7,
};

// ============================================================================
// 通用结果类型
// ============================================================================
struct Result {
    ErrorCode code = ErrorCode::OK;
    std::string message;

    bool ok() const { return code == ErrorCode::OK; }

    static Result success(const std::string& msg = "") {
        return {ErrorCode::OK, msg};
    }

    static Result error(ErrorCode code, const std::string& msg) {
        return {code, msg};
    }
};

} // namespace robot_runtime
