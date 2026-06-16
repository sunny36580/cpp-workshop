#include "runtime/core/service.h"

#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <chrono>
#include <ctime>
#include <sstream>
#include <fstream>
#include <iostream>

namespace robot_runtime {

// ============================================================================
// ProcessService 实现
// ============================================================================
// 核心思想：Runtime 不感知服务类型。
// start = cd path && bash start.sh
// stop  = cd path && bash stop.sh (fallback: SIGTERM)
// ============================================================================
ProcessService::ProcessService(std::string name,
                               std::string path,
                               std::vector<std::string> depends,
                               bool auto_restart)
    : name_(std::move(name))
    , path_(std::move(path))
    , depends_(std::move(depends))
    , auto_restart_(auto_restart)
{
}

ProcessService::~ProcessService() {
    if (state_ == ServiceState::RUNNING) {
        stop();
    }
}

// ---------------------------------------------------------------------------
static int fork_and_exec(const std::string& work_dir,
                         const std::string& cmd,
                         const std::string& log_path) {
    // 打开日志
    int log_fd = -1;
    if (!log_path.empty()) {
        log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    }

    // 写时间戳头
    if (log_fd >= 0) {
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        char time_buf[64];
        std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&tt));
        dprintf(log_fd, "\n%s\n", std::string(60, '=').c_str());
        dprintf(log_fd, "[%s]\n", time_buf);
        dprintf(log_fd, "%s\n", std::string(60, '=').c_str());
    }

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // 子进程
        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }
        signal(SIGPIPE, SIG_IGN);

        if (!work_dir.empty()) {
            chdir(work_dir.c_str());
        }

        // cmd = "bash start.sh" → bash -c "cd path && bash start.sh"
        execlp("bash", "bash", "-c", cmd.c_str(), nullptr);
        perror("execlp failed");
        _exit(127);
    }

    if (log_fd >= 0) close(log_fd);
    return pid;
}

// ---------------------------------------------------------------------------
bool ProcessService::start() {
    if (state_ == ServiceState::RUNNING) return true;

    // 构建命令: cd path && bash start.sh
    std::string abs_path = workspace_ + "/" + path_;
    std::string cmd = "cd " + abs_path + " && bash start.sh";
    std::string log_path = log_dir_ + "/services/" + name_ + ".log";

    state_ = ServiceState::STARTING;

    pid_t pid = fork_and_exec(abs_path, cmd, log_path);
    if (pid < 0) {
        fprintf(stderr, "[%s] fork failed\n", name_.c_str());
        state_ = ServiceState::FAILED;
        return false;
    }

    pid_ = pid;
    state_ = ServiceState::RUNNING;
    printf("[%s] started (PID=%d)\n", name_.c_str(), pid_);
    return true;
}

// ---------------------------------------------------------------------------
bool ProcessService::stop() {
    if (pid_ <= 0) {
        state_ = ServiceState::STOPPED;
        return true;
    }

    printf("[%s] stopping (PID=%d)\n", name_.c_str(), pid_);
    state_ = ServiceState::STOPPING;

    // 1) 先尝试 stop.sh
    std::string abs_path = workspace_ + "/" + path_;
    std::string stop_script = abs_path + "/stop.sh";
    if (access(stop_script.c_str(), X_OK) == 0) {
        std::string cmd = "cd " + abs_path + " && bash stop.sh";
        pid_t stop_pid = fork_and_exec(abs_path, cmd, "");
        if (stop_pid > 0) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (std::chrono::steady_clock::now() < deadline) {
                int status;
                if (waitpid(stop_pid, &status, WNOHANG) == stop_pid) break;
                usleep(100000);
            }
        }
    }

    // 2) SIGTERM
    kill(pid_, SIGTERM);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        int status;
        pid_t ret = waitpid(pid_, &status, WNOHANG);
        if (ret == pid_) {
            pid_ = 0;
            state_ = ServiceState::STOPPED;
            printf("[%s] stopped\n", name_.c_str());
            return true;
        }
        usleep(100000); // 100ms
    }

    // 4) 超时，强制 SIGKILL
    printf("[%s] timeout, sending SIGKILL\n", name_.c_str());
    kill(pid_, SIGKILL);
    waitpid(pid_, nullptr, 0);
    pid_ = 0;
    state_ = ServiceState::STOPPED;
    return true;
}

// ---------------------------------------------------------------------------
bool ProcessService::restart() {
    stop();
    return start();
}

// ---------------------------------------------------------------------------
bool ProcessService::is_alive() const {
    if (pid_ <= 0) return false;
    return kill(pid_, 0) == 0;
}

// ---------------------------------------------------------------------------
ServiceStatus ProcessService::status() const {
    ServiceStatus s;
    s.name   = name_;
    s.state  = state_;
    s.pid    = is_alive() ? pid_ : 0;
    s.alive  = is_alive();
    s.depends = depends_;
    return s;
}

} // namespace robot_runtime
