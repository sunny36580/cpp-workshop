#pragma once

#include <string>
#include <chrono>
#include <thread>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <fcntl.h>

namespace robot_runtime {

// ============================================================================
// ProcessUtils — 进程操作工具函数
// ============================================================================

inline void reap_zombies() {
    while (true) {
        int status;
        pid_t ret = waitpid(-1, &status, WNOHANG);
        if (ret <= 0) break;
    }
}

inline pid_t fork_and_exec(const std::string& work_dir,
                           const std::string& cmd,
                           const std::string& log_path = "",
                           pid_t* out_pgid = nullptr) {
    int log_fd = -1;
    if (!log_path.empty()) {
        log_fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
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
    if (out_pgid) *out_pgid = pid;
    return pid;
}

inline bool stop_process_group(pid_t pid, pid_t pgid) {
    if (pid <= 0) return true;

    if (pgid > 0) {
        kill(-pgid, SIGTERM);
    } else {
        kill(pid, SIGTERM);
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            reap_zombies();
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (pgid > 0) {
        kill(-pgid, SIGKILL);
    } else {
        kill(pid, SIGKILL);
    }
    waitpid(pid, nullptr, 0);
    reap_zombies();
    return true;
}

inline bool is_pid_alive(pid_t pid) {
    return pid > 0 && kill(pid, 0) == 0;
}

} // namespace robot_runtime
