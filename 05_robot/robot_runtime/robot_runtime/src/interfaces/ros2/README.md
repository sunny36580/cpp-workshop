# ROS2 接口定义

Runtime 与外部服务之间通过 ROS2 的 Topic 和 Service 通信，所有接口定义集中在此包中。

## 通信总览

```
┌─────────────────────────┐      Topic (/runtime/heartbeat)      ┌──────────────────┐
│  外部服务 (slam/nav/…)  │ ──────────────────────────────────→  │  Runtime          │
│                         │      Heartbeat.msg (定时上报)         │  Ros2HeartbeatSource
│                         │                                      │       ↓           │
│                         │ ←─ Service (/svc_name/lifecycle) ──  │  HeartbeatMonitor │
│                         │      LifecycleCommand.srv            │       ↓           │
│                         │      (activate/deactivate)           │  ModeManager      │
│                         │                                      │       ↓           │
│                         │ ←─ Service (/svc_name/get_state) ──  │  ServiceAccessMgr │
│                         │      GetServiceState.srv             │  Ros2ServiceClient│
└─────────────────────────┘                                      └──────────────────┘
```

## 消息定义

### Heartbeat.msg

所有服务的统一心跳上报格式，通过 `/runtime/heartbeat` 话题发布。

| 字段 | 类型 | 说明 |
|------|------|------|
| `service_name` | string | 服务唯一标识名 |
| `timestamp` | float64 | 心跳时间戳（epoch 秒） |
| `state` | string | 服务状态: `online` / `offline` / `unhealthy` |
| `message` | string | 附加信息 |

**服务端发布示例：**

```cpp
void publishHeartbeat() {
    robot_runtime_ros2_interfaces::msg::Heartbeat hb;
    hb.service_name = "module_manager_hub";
    hb.timestamp = this->now().seconds();
    hb.state = active_ ? "online" : "inactive";
    hb_pub_->publish(hb);
}
```

### ServiceState.msg

服务详细状态快照，由 `GetServiceState.srv` 返回。

| 字段 | 类型 | 说明 |
|------|------|------|
| `service_name` | string | 服务名称 |
| `state` | string | `running` / `stopped` / `activating` / `deactivating` |
| `ready` | bool | 是否就绪 |
| `active` | bool | 是否激活 |
| `health` | string | `healthy` / `degraded` / `unhealthy` |
| `message` | string | 附加信息 |

## Service 定义

### LifecycleCommand.srv

Runtime 向服务下发激活/停用指令。

**Request:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `command` | string | `activate` 或 `deactivate` |
| `service_name` | string | 目标服务名 |

**Response:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `success` | bool | 是否成功 |
| `message` | string | 结果信息 |

服务端在 `/{service_name}/lifecycle` 提供此 service。

**Client 端调用 (Runtime 侧)：**

```cpp
// 已通过 get_cached_client 缓存复用
auto client = get_cached_client<LifecycleCommand>(service, "/lifecycle");
auto req = std::make_shared<LifecycleCommand::Request>();
req->command = "activate";
auto future = client->async_send_request(req);
```

**服务端实现示例：**

```cpp
rclcpp::Service<robot_runtime_ros2_interfaces::srv::LifecycleCommand>::SharedPtr srv_;

srv_ = create_service<robot_runtime_ros2_interfaces::srv::LifecycleCommand>(
    "/module_manager_hub/lifecycle",
    [this](const std::shared_ptr<LifecycleCommand::Request> req,
           std::shared_ptr<LifecycleCommand::Response> resp) {
        if (req->command == "activate") {
            active_ = true;
            resp->success = true;
            resp->message = "activated";
        } else if (req->command == "deactivate") {
            active_ = false;
            resp->success = true;
            resp->message = "deactivated";
        } else {
            resp->success = false;
            resp->message = "unknown command: " + req->command;
        }
    });
```

### GetServiceState.srv

Runtime 按需查询服务详细状态。

**Request:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `service_name` | string | 目标服务名 |

**Response:**

| 字段 | 类型 | 说明 |
|------|------|------|
| `service_name` | string | 服务名称 |
| `state` | string | 服务状态 |
| `ready` | bool | 是否就绪 |
| `active` | bool | 是否激活 |
| `health` | string | 健康状态 |
| `message` | string | 附加信息 |

服务端在 `/{service_name}/get_state` 提供此 service。

## 服务端接入清单

任何需要被 Runtime 管控的外部服务，需要实现以下接口：

| 接口 | 类型 | 地址 | 必须 | 说明 |
|------|------|------|:----:|------|
| `/runtime/heartbeat` | Topic (Heartbeat.msg) | 发布 | ✅ | 定时上报心跳 |
| `/{name}/lifecycle` | Service (LifecycleCommand.srv) | 提供 | 托管型 | 接收 activate/deactivate |
| `/{name}/get_state` | Service (GetServiceState.srv) | 提供 | 可选 | 按需查询状态 |

### minimal — 仅心跳上报

```cpp
// 只 publish heartbeat，不做 lifecycle service
hb_pub_ = create_publisher<robot_runtime_ros2_interfaces::msg::Heartbeat>(
    "/runtime/heartbeat", 10);
```

### managed — 完整生命周期

```cpp
// 心跳 + lifecycle + get_state 全部实现
hb_pub_       = create_publisher<Heartbeat>("/runtime/heartbeat", 10);
lc_srv_       = create_service<LifecycleCommand>("/svc_name/lifecycle", …);
get_state_srv_ = create_service<GetServiceState>("/svc_name/get_state", …);
```

## Runtime 侧接入

Runtime 侧对应的 adapter：

| 适配器 | 文件 | 用途 |
|--------|------|------|
| `Ros2HeartbeatSource` | `adapter/ros2/ros2_heartbeat_source.cpp` | 订阅 `/runtime/heartbeat` |
| `Ros2ServiceClient` | `adapter/ros2/ros2_service_client.cpp` | 调用 LifecycleCommand / GetState |

均在编译时通过 `HAS_ROS2_INTERFACES` 宏启用，无 ROS2 环境时编译为桩实现。

## 构建

```bash
# 在 robot_runtime 项目内构建（自动检测 ROS2 + rosidl）
cd robot_runtime && ./build.sh

# 或在 ROS2 工作区中独立构建接口包
cd ros2_ws && colcon build --packages-select robot_runtime_ros2_interfaces
```
