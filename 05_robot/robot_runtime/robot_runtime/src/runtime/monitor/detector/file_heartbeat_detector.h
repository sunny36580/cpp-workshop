#pragma once

#include <string>
#include <chrono>

namespace robot_runtime {

/// 文件心跳检测器
/// 仅封装文件系统操作：stat 获取修改时间、创建/清空目录
/// 无网络/ROS 依赖，纯文件操作
class FileHeartbeatDetector {
public:
    FileHeartbeatDetector() = default;

    /// 配置
    void set_heartbeat_dir(const std::string& dir) { heartbeat_dir_ = dir; }
    void set_timeout_sec(double sec)               { timeout_sec_ = sec; }

    const std::string& heartbeat_dir() const { return heartbeat_dir_; }
    double timeout_sec() const               { return timeout_sec_; }

    /// 创建心跳目录
    bool create_dir();

    /// 清空心跳目录（删除所有文件）
    void clear_dir();

    /// 检测指定服务是否心跳超时
    /// 返回 true=超时（文件不存在或 mtime 超过阈值）
    ///      false=正常
    bool check(const std::string& service_name);

    /// 获取文件修改时间（秒，epoch）
    static double get_file_mtime(const std::string& path);

private:
    std::string heartbeat_dir_ = "/tmp/runtime_hb";
    double timeout_sec_ = 8.0;
};

} // namespace robot_runtime
