#include "runtime/core/service.h"
#include "runtime/common/process_manager.h"

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
// start = cd path && bash start.sh（fork+setsid 新建进程组）
// stop  = cd path && bash stop.sh → SIGTERM → 3s → SIGKILL 批量清理进程组
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
// fork + setsid + exec，父进程记录 pid 和 pgid
// ---------------------------------------------------------------------------
static pid_t fork_and_exec(const std::string& work_dir,
                           const std::string& cmd,
                           const std::string& log_path,
                           pid_t* out_pgid = nullptr) {
    // 打开日志
    int log_fd = -1;
    if (!log_path.empty()) {
        log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);

        // 写时间戳头
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
        // ---- 子进程 ----
        // 新建会话和进程组，与父进程脱离
        setsid();
        signal(SIGPIPE, SIG_IGN);

        if (log_fd >= 0) {
            dup2(log_fd, STDOUT_FILENO);
            dup2(log_fd, STDERR_FILENO);
            close(log_fd);
        }

        if (!work_dir.empty()) {
            [[maybe_unused]] int cd_ret = chdir(work_dir.c_str());
            (void)cd_ret;
        }

        execlp("bash", "bash", "-c", cmd.c_str(), nullptr);
        perror("execlp failed");
        _exit(127);
    }

    if (log_fd >= 0) close(log_fd);

    // 返回进程组 ID（子进程 setsid 后其 pgid = pid）
    if (out_pgid) *out_pgid = pid;
    return pid;
}

// ---------------------------------------------------------------------------
// 收割僵尸进程（非阻塞）
// ---------------------------------------------------------------------------
static void reap_zombies() {
    while (true) {
        int status;
        pid_t ret = waitpid(-1, &status, WNOHANG);
        if (ret <= 0) break;
    }
}

// ---------------------------------------------------------------------------
bool ProcessService::start() {
    if (state_ == ServiceState::RUNNING) return true;

    // 构建命令: cd path && bash start.sh
    // 规范化路径：处理 workspace=. + path=./services/motion 的情况
    std::string abs_path;
    if (!path_.empty() && path_[0] == '/') {
        abs_path = path_;
    } else if (!workspace_.empty() && workspace_ != ".") {
        abs_path = workspace_ + "/" + path_;
    } else {
        abs_path = path_;
    }
    std::string cmd = "cd " + abs_path + " && bash start.sh";
    std::string log_path = log_dir_ + "/services/" + name_ + ".log";

    state_ = ServiceState::STARTING;

    pid_t child_pgid = 0;
    pid_t pid = fork_and_exec(abs_path, cmd, log_path, &child_pgid);
    if (pid < 0) {
        fprintf(stderr, "[%s] fork failed\n", name_.c_str());
        state_ = ServiceState::FAILED;
        return false;
    }

    pid_ = pid;
    pgid_ = child_pgid;
    state_ = ServiceState::RUNNING;
    printf("[%s] started (PID=%d, PGID=%d)\n", name_.c_str(), pid_, pgid_);
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
    // 规范化路径：处理 workspace=. + path=./services/motion 的情况
    std::string abs_path;
    if (!path_.empty() && path_[0] == '/') {
        abs_path = path_;
    } else if (!workspace_.empty() && workspace_ != ".") {
        abs_path = workspace_ + "/" + path_;
    } else {
        abs_path = path_;
    }
    std::string stop_script = abs_path + "/stop.sh";
    if (access(stop_script.c_str(), X_OK) == 0) {
        std::string cmd = "cd " + abs_path + " && bash stop.sh";
        pid_t stop_pid = fork_and_exec(abs_path, cmd, "");
        if (stop_pid > 0) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < deadline) {
                int status;
                if (waitpid(stop_pid, &status, WNOHANG) == stop_pid) break;
                usleep(100000);
            }
            // 不管 stop.sh 是否完成，继续 SIGTERM
        }
    }

    // 2) SIGTERM 发给整个进程组，优雅退出
    if (pgid_ > 0) {
        kill(-pgid_, SIGTERM);
    } else {
        kill(pid_, SIGTERM);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        int status;
        pid_t ret = waitpid(pid_, &status, WNOHANG);
        if (ret == pid_) {
            reap_zombies();
            pid_ = 0;
            pgid_ = 0;
            state_ = ServiceState::STOPPED;
            printf("[%s] stopped gracefully\n", name_.c_str());
            return true;
        }
        usleep(100000); // 100ms
    }

    // 3) 超时，SIGKILL 批量清理整个进程组
    printf("[%s] timeout (3s), sending SIGKILL to process group\n", name_.c_str());
    if (pgid_ > 0) {
        kill(-pgid_, SIGKILL);
    } else {
        kill(pid_, SIGKILL);
    }
    waitpid(pid_, nullptr, 0);
    reap_zombies();
    pid_ = 0;
    pgid_ = 0;
    state_ = ServiceState::STOPPED;
    printf("[%s] force killed\n", name_.c_str());
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
