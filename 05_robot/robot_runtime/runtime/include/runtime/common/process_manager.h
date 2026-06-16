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
// ProcessManager — 进程管理工具
// ============================================================================
// 负责 fork + setsid + exec 执行 shell 命令，管理进程生命周期。
// 启动：fork 后 setsid() 新建进程组，方便批量清理。
// 停止：SIGTERM → 3s 超时 → SIGKILL 批量清理整个进程组。
// ============================================================================
class ProcessManager {
public:
    ProcessManager() = default;
    ~ProcessManager() { stop(); }

    // 启动进程（进入工作目录后执行命令）
    bool start(const std::string& work_dir, const std::string& cmd,
               const std::string& log_path = "") {
        stop();

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            return false;
        }

        if (pid == 0) {
            // ---- 子进程 ----
            // 新建会话、进程组，与父进程完全脱离
            setsid();
            signal(SIGPIPE, SIG_IGN);

            if (!work_dir.empty()) {
                chdir(work_dir.c_str());
            }

            // 日志重定向
            if (!log_path.empty()) {
                int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd >= 0) {
                    dup2(fd, STDOUT_FILENO);
                    dup2(fd, STDERR_FILENO);
                    close(fd);
                }
            }

            // 执行命令
            execlp("bash", "bash", "-c", cmd.c_str(), nullptr);
            perror("execlp failed");
            _exit(127);
        }

        pid_ = pid;
        pgid_ = pid;  // setsid 后子进程的进程组 ID = pid
        return true;
    }

    // 停止进程（SIGTERM → 3s 超时 → SIGKILL 批量清理整个进程组）
    bool stop() {
        if (pid_ <= 0) return true;

        // 1) SIGTERM 优雅退出（发给整个进程组）
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
                // 回收子进程，同时收割可能残留的僵尸
                reap_zombies();
                pid_ = 0;
                pgid_ = 0;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 2) 超时，SIGKILL 批量清理整个进程组
        if (pgid_ > 0) {
            kill(-pgid_, SIGKILL);
        } else {
            kill(pid_, SIGKILL);
        }
        waitpid(pid_, nullptr, 0);

        // 收割可能残留的僵尸进程
        reap_zombies();
        pid_ = 0;
        pgid_ = 0;
        return true;
    }

    // 收割僵尸进程（非阻塞）
    void reap_zombies() {
        while (true) {
            int status;
            pid_t ret = waitpid(-1, &status, WNOHANG);
            if (ret <= 0) break;
        }
    }

    bool is_alive() const {
        return pid_ > 0 && kill(pid_, 0) == 0;
    }

    int pid() const { return pid_; }
    int pgid() const { return pgid_; }

private:
    pid_t pid_ = 0;
    pid_t pgid_ = 0;
};

} // namespace robot_runtime
