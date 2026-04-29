#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

using namespace std;
const int PORT = 8888;
const int BUF_SIZE = 1024 * 1024;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cout << "Usage: ./zc_client <server_ip>\n";
        return 1;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, argv[1], &addr.sin_addr);
    connect(fd, (sockaddr*)&addr, sizeof(addr));

    char buf[BUF_SIZE];
    while (recv(fd, buf, BUF_SIZE, 0) > 0);

    close(fd);
    cout << "Client receive complete!\n";
    return 0;
}