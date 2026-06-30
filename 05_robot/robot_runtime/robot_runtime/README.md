# Robot Runtime

机器人系统运行时管控底座。提供服务的进程管理、依赖排序、健康检测、模式切换能力，支持 CLI 和 TCP 双通道远程管控。

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
┌──────────────────────────────────────────────┐
│  gateway/     CLI + TCP 远程管控              │
├──────────────────────────────────────────────┤
│  orchestration/  模式编排、场景调度             │
├──────────────────────────────────────────────┤
│  runtime/        进程管理、依赖排序、健康检测    │
│    ├── process/service_manager   服务启停      │
│    ├── process/dependency_manager 依赖拓扑排序  │
│    └── monitor                   心跳检测+监控  │
├──────────────────────────────────────────────┤
│  common/        公共基础 (header-only)        │
├──────────────────────────────────────────────┤
│  core/          Runtime 主类，组合所有层       │
└──────────────────────────────────────────────┘
```

### 预留扩展层（当前不参与编译）

```
capability/   能力抽象接口（占位）
adapter/      通信适配层（预留）
behavior/     行为编排（预留）
plugins/      插件分类（预留）
```

## 快速开始

### 构建

```bash
# 默认构建（含单元测试）
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
| `monitor.yaml` | 健康检测配置 |
| `cli.yaml` | CLI 配置 |
| `network.yaml` | TCP 远程管控配置 |

### 服务注册示例

```yaml
services:
  module_manager_hub:
    path: ./services/module_manager_hub
    description: 运控代理服务
    type: ros2
    launch_cmd: ros2 launch module_manager_hub manager.launch.py
    depends: []
```

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

```bash
# 构建并运行全部测试
./build.sh

# 单独运行测试（无需重新打包）
cd build && ctest --output-on-failure

# 运行单个测试套件
./build/src/tests/runtime_unit_tests
./build/src/tests/orchestration_unit_tests
./build/src/tests/gateway_unit_tests
```

### 测试覆盖

| 模块 | 测试文件 | 内容 |
|------|---------|------|
| dependency_manager | `tests/unit/runtime/dependency_manager_test.cpp` | 拓扑排序（单节点、链式、菱形、无依赖） |
| file_heartbeat_detector | `tests/unit/runtime/file_heartbeat_detector_test.cpp` | 构造、配置、超时检测 |
| mode_manager | `tests/unit/orchestration/mode_manager_test.cpp` | 可编译性验证 |
| protocol_parser | `tests/unit/gateway/protocol_parser_test.cpp` | 可编译性验证 |
| command_dispatcher | `tests/unit/gateway/command_dispatcher_test.cpp` | 可编译性验证 |

## 安装部署

```bash
./build.sh
# 产物在 install/ 目录下：
#   install/
#   ├── bin/robot
#   ├── lib/
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
- GoogleTest（单元测试，仅在 `BUILD_TESTING=ON` 时需要）

### Ubuntu 安装依赖

```bash
sudo apt install cmake build-essential libyaml-cpp-dev libgtest-dev
```

## 项目结构

```
robot_runtime/
├── CMakeLists.txt              # 顶层 CMake
├── build.sh                    # 一键构建脚本
├── start_runtime.sh            # 启动入口
├── setup.bash / local_setup.bash  # 环境设置
├── config/                     # YAML 配置文件
├── 3rd_party/                  # 第三方依赖
├── src/
│   ├── CMakeLists.txt          # 分层编译配置
│   ├── common/                 # 公共基础 (header-only)
│   ├── core/                   # Runtime 主类
│   ├── gateway/
│   │   ├── cli/                # 命令行入口
│   │   └── tcp/                # TCP 远程管控协议
│   ├── orchestration/mode/     # 模式编排
│   ├── runtime/
│   │   ├── process/            # 进程管理 + 依赖排序
│   │   └── monitor/            # 健康检测
│   └── tests/                  # 单元测试
│       ├── unit/
│       │   ├── runtime/
│       │   ├── orchestration/
│       │   └── gateway/
│       ├── mock/
│       └── integration/
├── services/                   # 外部服务实现（ROS节点/Python/二进制）
├── tools/                      # 运维脚本
└── log/                        # 运行时日志
```

## License

Internal use.
