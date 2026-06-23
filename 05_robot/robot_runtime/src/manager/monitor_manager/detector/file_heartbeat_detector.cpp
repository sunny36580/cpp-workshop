#include "manager/monitor_manager/detector/file_heartbeat_detector.h"

#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace robot_runtime {

bool FileHeartbeatDetector::create_dir()
{
    try {
        fs::create_directories(heartbeat_dir_);
        return true;
    } catch (const fs::filesystem_error& e) {
        fprintf(stderr, "[FileHeartbeatDetector] 创建目录失败 %s: %s\n",
                heartbeat_dir_.c_str(), e.what());
        return false;
    }
}

void FileHeartbeatDetector::clear_dir()
{
    try {
        if (!fs::exists(heartbeat_dir_)) return;
        for (const auto& entry : fs::directory_iterator(heartbeat_dir_)) {
            fs::remove(entry.path());
        }
        printf("[FileHeartbeatDetector] 清空心跳目录: %s\n", heartbeat_dir_.c_str());
    } catch (const fs::filesystem_error& e) {
        fprintf(stderr, "[FileHeartbeatDetector] 清空目录失败: %s\n", e.what());
    }
}

bool FileHeartbeatDetector::check(const std::string& service_name)
{
    std::string path = heartbeat_dir_ + "/" + service_name;

    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        // 文件不存在 → 超时
        return true;
    }

    double mtime = static_cast<double>(st.st_mtime);
    double now;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    now = static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;

    return (now - mtime) > timeout_sec_;
}

double FileHeartbeatDetector::get_file_mtime(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return 0.0;
    }
    return static_cast<double>(st.st_mtime);
}

} // namespace robot_runtime
