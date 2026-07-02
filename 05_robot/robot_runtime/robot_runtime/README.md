# Robot Runtime

机器人系统运行时管控底座。提供服务的进程管理、依赖排序、健康检测、模式切换能力，支持 CLI 和 TCP 双通道远程管控，并通过适配器机制接入 ROS2 等通信协议。

## 架构

```
gateway (CLI / TCP)
    ↓  管控指令
orchestration (mode_manager)
    ↓  场景调度
runtime (进程启停 + 依赖排序 + 健康检测)
    ↓  进程操作
外部服务 (ROS 节点 / Python 脚本 / 二进制)
```

### 分层设计

```
┌───────────────────────────────────────────────────┐
│  gateway/        CLI + TCP 远程管控                │
├───────────────────────────────────────────────────┤
│  orchestration/  模式编排、场景调度                  │
├───────────────────────────────────────────────────┤
│  runtime/        进程管理 + 依赖排序 + 心跳监控       │
│    ├── process/service_manager     服务启停         │
│    ├── process/dependency_manager  依赖拓扑排序      │
│    ├── monitor/heartbeat           心跳状态机 + 抽象接口│
│    └── service_access              服务注册/代理/调用 │
├───────────────────────────────────────────────────┤
│  adapter/        ROS2 / TCP / AimRT 具体协议适配     │
├───────────────────────────────────────────────────┤
│  common/         公共基础 (header-only)             │
├───────────────────────────────────────────────────┤
│  core/           Runtime 主类，组合所有层            │
└───────────────────────────────────────────────────┘
```

### 依赖方向（编译期强制）

```
common_lib (INTERFACE)
    ↑
runtime_lib  ←  orchestration_lib
    ↑                ↑
capability_lib (INTERFACE)
    ↑
gateway_lib
    ↑
adapter_lib  →  主程序 robot
```

### 心跳数据流

```
ROS2 话题 (/heartbeat/xxx)
    ↓ Ros2HeartbeatSource (adapter/ros2/) 订阅
HeartbeatMonitor::OnHeartbeat(event)
    ↓ 状态机 + 后台超时检测线程
Online / Timeout 状态变化回调
```

`IHeartbeatSource` 抽象接口定义在 `runtime/monitor/heartbeat/`，具体实现在 `adapter/` 层，core 只依赖接口不依赖实现。

### 预留扩展层（当前不参与编译）

```
capability/   能力抽象接口（占位）
behavior/     行为编排（预留）
plugins/      插件分类（预留）
```

## 快速开始

### 构建

```bash
# 默认构建（含单元测试 + 集成测试）
./build.sh

# 跳过测试
BUILD_TESTING=OFF ./build.sh
```

### 运行

```bash
# 列出所有服务
./start_runtime.sh list

# 启动默认模式
./start_runtime.sh up

# 查看服务状态
./start_runtime.sh status

# 切换模式
./start_runtime.sh mode switch teleop

# 常驻运行（启用 TCP 远程管控）
./start_runtime.sh daemon
```

`start_runtime.sh` 会自动判断处于源码树还是安装目录，统一入口。

### CLI 命令

| 命令 | 说明 |
|------|------|
| `robot list` | 列出所有服务及描述 |
| `robot status [service]` | 查看服务状态 |
| `robot start <service>` | 启动服务 |
| `robot stop <service>` | 停止服务 |
| `robot restart <service>` | 重启服务 |
| `robot mode list` | 列出所有模式 |
| `robot mode switch <mode>` | 切换模式 |
| `robot up` | 启动默认模式 |
| `robot down` | 停止所有服务 |
| `robot daemon` | 常驻模式（开启 TCP 远程管控） |

### 配置文件

所有配置在 `config/` 目录下：

| 文件 | 说明 |
|------|------|
| `services.yaml` | 服务注册：路径、类型、启动命令、依赖 |
| `modes.yaml` | 模式定义：每个模式包含的服务组合 |
| `runtime.yaml` | 运行时参数 |
| `monitor.yaml` | 监控 + ROS2 心跳话题配置 |
| `cli.yaml` | CLI 配置 |
| `network.yaml` | TCP 远程管控配置 |

### 服务注册示例

```yaml
services:
  module_manager_hub:
    path: ./services/module_manager_hub
    description: 运控代理服务
    type: ros2
    depends: []
    auto_restart: false
```

每个服务目录需要包含 `start.sh`（启动入口），可选 `stop.sh`（停止钩子）。

### 模式定义示例

```yaml
modes:
  standby:     { services: [motion] }
  teleop:      { services: [motion, remote_control] }
  interaction: { services: [motion, conversation] }
  debug:       { services: [all] }
  default: teleop
```

## 测试

构建时默认启测试，共 47 个用例。

```bash
# 构建并运行全部测试
./build.sh

# 单独运行测试
cd build && ctest --output-on-failure

# 运行单个测试套件
./build/src/tests/runtime_unit_tests
./build/src/tests/stage0_lifecycle_test
./build/src/tests/common_unit_tests
```

### 单元测试（29 tests）

| 模块 | 测试文件 | 内容 |
|------|---------|------|
| dependency_manager | `unit/runtime/dependency_manager_test.cpp` | 拓扑排序 + DependencyManager |
| service_manager | `unit/runtime/service_manager_test.cpp` | 配置加载、注册、启停 |
| mode_manager | `unit/orchestration/mode_manager_test.cpp` | 模式加载、切换 |
| config_loader | `unit/common/config_loader_test.cpp` | YAML 解析、异常处理 |

### 阶段 0 集成测试（18 tests）

用 fake service（`start.sh` + 临时目录）验证 Runtime 核心管控能力，不依赖真实机器人服务。

| 测试文件 | 测试数 | 验证内容 |
|---------|:------:|---------|
| `stage0_smoke_test.cpp` | 5 | 配置加载、坏配置拒绝、循环依赖安全 |
| `stage0_service_lifecycle_test.cpp` | 4 | 启停正常服务、崩溃服务、幂等性、不存在服务 |
| `stage0_dependency_order_test.cpp` | 3 | 依赖顺序启动、反向顺序停止 |
| `stage0_mode_switch_test.cpp` | 4 | idle/manual/auto 模式切换、不存在的模式 |
| `stage0_recovery_test.cpp` | 2 | kill 检测、崩溃服务启动 |

## 安装部署

```bash
./build.sh
# 产物在 install/ 目录下：
#   install/
#   ├── bin/robot
#   ├── lib/libruntime_lib.a
#   ├── config/*.yaml
#   ├── services/
#   ├── tools/*.sh
#   └── start_runtime.sh
```

部署后：

```bash
source install/setup.bash      # 加入 PATH
cd install && ./start_runtime.sh list
```

## 依赖

- C++17 编译器（GCC ≥ 9 / Clang ≥ 10）
- CMake ≥ 3.16
- yaml-cpp（YAML 解析）
- pthread（多线程）
- GoogleTest（单元测试，`BUILD_TESTING=ON` 时需要）
- ROS2 Humble（可选，启用 `adapter_lib` 的 ROS2 心跳订阅）

### Ubuntu 安装依赖

```bash
sudo apt install cmake build-essential libyaml-cpp-dev libgtest-dev

# ROS2 支持（可选）
sudo apt install ros-humble-desktop
source /opt/ros/humble/setup.bash
```

无 ROS2 环境时 `adapter_lib` 编译为桩实现（仅日志提示），不影响其他功能。

## 项目结构

```
robot_runtime/
├── CMakeLists.txt                  # 顶层 CMake
├── build.sh                        # 一键构建脚本
├── start_runtime.sh                # 启动入口
├── setup.bash / local_setup.bash   # 环境设置
├── config/                         # YAML 配置文件
├── 3rd_party/                      # 第三方依赖
├── src/
│   ├── CMakeLists.txt              # 分层编译配置
│   ├── common/                     # 公共基础 (header-only)
│   ├── core/                       # Runtime 主类
│   ├── gateway/
│   │   ├── cli/                    # 命令行入口
│   │   └── tcp/                    # TCP 远程管控协议
│   ├── orchestration/mode/         # 模式编排
│   ├── runtime/
│   │   ├── process/
│   │   │   ├── service_manager/    # 进程服务启停
│   │   │   └── dependency_manager/ # 依赖拓扑排序
│   │   ├── monitor/
│   │   │   └── heartbeat/          # 心跳状态机 + IHeartbeatSource 接口
│   │   └── service_access/         # 服务注册、代理、调用
│   ├── adapter/
│   │   ├── ros2/                   # ROS2 心跳订阅适配器
│   │   ├── tcp/                    # 预留
│   │   ├── aimrt/                  # 预留
│   │   └── python/                 # 预留
│   └── tests/
│       ├── unit/
│       │   ├── runtime/
│       │   ├── orchestration/
│       │   └── common/
│       └── integration/stage0/     # 阶段 0 集成测试（含 fake service）
├── services/                       # 外部服务实现（ROS节点/Python/二进制）
├── tools/                          # 运维脚本
└── log/                            # 运行时日志
```

## License

Internal use.
