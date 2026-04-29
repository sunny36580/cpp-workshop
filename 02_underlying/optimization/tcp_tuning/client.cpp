#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <vector>
#include <sys/uio.h>
#include <getopt.h>

using namespace std;
using namespace chrono;
const size_t TOTAL_BYTES = 1ULL * 1024 * 1024 * 1024; // 固定发1GB

struct Args {
    string ip = "127.0.0.1";
    int buf_size = 64 * 1024;
    bool no_nagle = false;
    bool use_writev = false;
};

void usage() {
    cout << "Usage: ./tcp_client -i <ip> -b <buffer(KB)> -n(no nagle) -v(use writev)\n";
    cout << "Example: ./tcp_client -i 192.168.122.100 -b 1024 -n -v\n";
}

Args parse_args(int argc, char* argv[]) {
    Args args;
    int opt;
    while ((opt = getopt(argc, argv, "i:b:nv")) != -1) {
        switch (opt) {
            case 'i': args.ip = optarg; break;
            case 'b': args.buf_size = atoi(optarg) * 1024; break;
            case 'n': args.no_nagle = true; break;
            case 'v': args.use_writev = true; break;
            default: usage(); exit(1);
        }
    }
    return args;
}

int main(int argc, char* argv[]) {
    Args args = parse_args(argc, argv);
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &args.buf_size, sizeof(args.buf_size));

    if (args.no_nagle) {
        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    inet_pton(AF_INET, args.ip.c_str(), &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }

    vector<char> buf(args.buf_size, 'A');
    size_t sent = 0;
    auto start = high_resolution_clock::now();

    if (args.use_writev) {
        iovec iov[4];
        for (int i = 0; i < 4; i++) { iov[i].iov_base = buf.data(); iov[i].iov_len = buf.size(); }
        while (sent < TOTAL_BYTES) {
            ssize_t n = writev(fd, iov, 4);
            if (n <= 0) break;
            sent += n;
        }
    } else {
        while (sent < TOTAL_BYTES) {
            ssize_t n = write(fd, buf.data(), buf.size());
            if (n <= 0) break;
            sent += n;
        }
    }

    double duration_s = duration_cast<microseconds>(high_resolution_clock::now() - start).count() / 1e6;
    double throughput = (TOTAL_BYTES / 1024.0 / 1024.0) / duration_s;
    printf("Throughput: %.2f MB/s\n", throughput);

    close(fd);
    return 0;
}