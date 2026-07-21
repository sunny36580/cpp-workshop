# 服务状态消息
# 由 Runtime 对外发布，或被 GetServiceState.srv 查询返回

string service_name    # 服务名称
string state           # "running", "stopped", "activating", "deactivating"
bool   ready           # 是否就绪
bool   active          # 是否激活
string health          # "healthy", "degraded", "unhealthy"
string message         # 附加信息
