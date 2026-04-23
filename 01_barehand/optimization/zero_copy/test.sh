# 服务端
# 1. 普通拷贝模式
taskset -c 0 ./zc_server normal

# 2. 零拷贝 sendfile 模式
taskset -c 0 ./zc_server sendfile

# 3. mmap+write 模式
taskset -c 0 ./zc_server mmap


# 客户端
taskset -c 1 ./zc_client 192.168.101.9
