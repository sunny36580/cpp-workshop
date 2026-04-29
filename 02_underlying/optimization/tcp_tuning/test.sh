# 基线：64KB buffer，默认配置
taskset -c 1 ./tcp_client -i 192.168.101.9  -b 64

# 1MB buffer
taskset -c 1 ./tcp_client -i 192.168.101.9  -b 1024

# 4MB buffer + 关闭Nagle
taskset -c 1 ./tcp_client -i 192.168.101.9  -b 4096 -n

# 最优配置：4MB + 关闭Nagle + writev
taskset -c 1 ./tcp_client -i 192.168.101.9  -b 4096 -n -v


# 服务端命令
taskset -c 0 ./tcp_server