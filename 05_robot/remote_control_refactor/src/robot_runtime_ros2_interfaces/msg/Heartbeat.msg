# 心跳消息
# 由外部服务定时发布，Runtime 的 Ros2HeartbeatSource 订阅后送入 HeartbeatMonitor

string service_name    # 服务名称
float64 timestamp      # 心跳时间戳 (epoch seconds)
string state           # 服务状态: "online", "offline", "unhealthy"
string message         # 附加信息
