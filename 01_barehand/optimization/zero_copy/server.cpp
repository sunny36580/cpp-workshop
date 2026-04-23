#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/mman.h>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <getopt.h>

using namespace std;
using namespace chrono;

const int PORT = 8888;
const char* FILE_PATH = "test.bin"; // 提前生成1GB文件
const int BUF_SIZE = 1024 * 1024;   // 1MB 普通拷贝缓冲区

// CPU 统计（和TCP调优一致，修复版）
struct CpuStat {
    uint64_t total;
    uint64_t idle;
};
CpuStat get_cpu_stat() {
    FILE* fp = fopen("/proc/stat", "r");
    CpuStat s{};
    uint64_t user, nice, sys, idle, iowait, irq, softirq;
    fscanf(fp, "cpu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &sys, &idle, &iowait, &irq, &softirq);
    fclose(fp);
    s.idle = idle + iowait;
    s.total = user + nice + sys + s.idle + irq + softirq;
    return s;
}

// 传输模式
enum Mode {
    NORMAL,    // read + send
    SENDFILE,  // 零拷贝
    MMAP       // mmap + write
};

// 1. 普通拷贝：read + send
void normal_transfer(int file_fd, int sock_fd, off_t file_size) {
    char* buf = new char[BUF_SIZE];
    off_t sent = 0;
    while (sent < file_size) {
        ssize_t n = read(file_fd, buf, BUF_SIZE);
        send(sock_fd, buf, n, 0);
        sent += n;
    }
    delete[] buf;
}

// 2. 零拷贝：sendfile
void sendfile_transfer(int file_fd, int sock_fd, off_t file_size) {
    sendfile(sock_fd, file_fd, nullptr, file_size);
}

// 3. mmap + write（模拟视频流）
void mmap_transfer(int file_fd, int sock_fd, off_t file_size) {
    void* map = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, file_fd, 0);
    write(sock_fd, map, file_size);
    munmap(map, file_size);
}

// 命令行参数
Mode parse_mode(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: ./zc_server <mode>\n";
        cout << "mode: normal | sendfile | mmap\n";
        exit(1);
    }
    string m = argv[1];
    if (m == "normal") return NORMAL;
    if (m == "sendfile") return SENDFILE;
    if (m == "mmap") return MMAP;
    exit(1);
}

int main(int argc, char* argv[]) {
    Mode mode = parse_mode(argc, argv);
    int file_fd = open(FILE_PATH, O_RDONLY);
    off_t file_size = lseek(file_fd, 0, SEEK_END);
    lseek(file_fd, 0, SEEK_SET);

    // Socket 初始化
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);

    cout << "Server waiting client...\n";
    int client_fd = accept(server_fd, nullptr, nullptr);

    // 开始统计
    auto start = high_resolution_clock::now();
    CpuStat cpu_start = get_cpu_stat();

    // 执行传输
    switch (mode) {
        case NORMAL: normal_transfer(file_fd, client_fd, file_size); break;
        case SENDFILE: sendfile_transfer(file_fd, client_fd, file_size); break;
        case MMAP: mmap_transfer(file_fd, client_fd, file_size); break;
    }

    // 结束统计
    auto end = high_resolution_clock::now();
    CpuStat cpu_end = get_cpu_stat();
    double duration = duration_cast<milliseconds>(end - start).count() / 1000.0;
    double cpu_usage = 100.0 * (1.0 - (double)(cpu_end.idle - cpu_start.idle)/(cpu_end.total - cpu_start.total));
    double throughput = (file_size / 1024.0 / 1024.0) / duration;

    // 输出结果
    cout << "\n=========================================\n";
    cout << "Mode: " << argv[1] << "\n";
    cout << "Time: " << duration << "s\n";
    cout << "CPU: " << cpu_usage << "%\n";
    cout << "Throughput: " << throughput << " MB/s\n";
    cout << "=========================================\n";

    close(client_fd);
    close(server_fd);
    close(file_fd);
    return 0;
}