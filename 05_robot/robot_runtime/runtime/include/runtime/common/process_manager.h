#pragma once

#include <string>
#include <chrono>
#include <thread>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>

namespace robot_runtime {

// ============================================================================
// ProcessManager — 进程管理工具
// ============================================================================
// 负责 fork + exec 执行 shell 命令，管理进程生命周期。
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
            // 子进程
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
        return true;
    }

    // 停止进程（SIGTERM → 5s → SIGKILL）
    bool stop() {
        if (pid_ <= 0) return true;

        kill(pid_, SIGTERM);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline) {
            int status;
            pid_t ret = waitpid(pid_, &status, WNOHANG);
            if (ret == pid_) {
                pid_ = 0;
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // 超时，强制杀死
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = 0;
        return true;
    }

    bool is_alive() const {
        return pid_ > 0 && kill(pid_, 0) == 0;
    }

    int pid() const { return pid_; }

private:
    int pid_ = 0;
};

} // namespace robot_runtime
