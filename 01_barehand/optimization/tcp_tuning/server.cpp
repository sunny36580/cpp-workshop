#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdint>

using namespace std;
using namespace chrono;

const int PORT = 8888;
const int RECV_BUF_SIZE = 4 * 1024 * 1024;

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

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &RECV_BUF_SIZE, sizeof(RECV_BUF_SIZE));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&addr, sizeof(addr));
    listen(server_fd, 1);
    cout << "Server listening on port " << PORT << endl;

    int client_fd = accept(server_fd, nullptr, nullptr);
    cout << "Client connected\n";

    char buf[RECV_BUF_SIZE];
    uint64_t total_bytes = 0;
    auto start_time = high_resolution_clock::now();
    CpuStat cpu_start = get_cpu_stat();

    while (true) {
        ssize_t n = recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        total_bytes += n;
    }

    auto end_time = high_resolution_clock::now();
    CpuStat cpu_end = get_cpu_stat();

    double duration_s = duration_cast<microseconds>(end_time - start_time).count() / 1e6;
    double throughput = (total_bytes / 1024.0 / 1024.0) / duration_s;
    double cpu_usage = 100.0 * (1.0 - (double)(cpu_end.idle - cpu_start.idle)/(cpu_end.total - cpu_start.total));

    printf("=========================================\n");
    printf("Total Recv: %.2f GB\n", total_bytes / 1024.0 / 1024.0 / 1024.0);
    printf("Throughput:  %.2f MB/s\n", throughput);
    printf("CPU Usage:   %.1f %% \n", cpu_usage);
    printf("=========================================\n");

    close(client_fd);
    close(server_fd);
    return 0;
}