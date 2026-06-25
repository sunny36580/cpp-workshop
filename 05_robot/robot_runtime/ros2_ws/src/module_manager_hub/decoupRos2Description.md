目标：

将当前 ROS2 模块重构为「Core + ROS Adapter」两层架构。

架构原则：

1. Core 层

* 不允许依赖 ROS2。
* 不允许包含：

  * rclcpp
  * std_msgs
  * sensor_msgs
  * geometry_msgs
  * nav_msgs
  * tf2
  * rosidl
* Core 只负责业务逻辑。
* Core 可以使用：

  * STL
  * Boost
  * OpenCV
  * FFmpeg
  * yaml-cpp
  * protobuf
  * 自定义结构体
* Core 对外暴露普通 C++ 接口。
* Core 不直接创建线程、Timer、ROS Publisher、ROS Subscriber。
* Core 不读取 ROS 参数。
* Core 不发布 ROS 消息。

2. ROS Adapter 层

* 继承 rclcpp::Node。
* 负责：

  * 参数读取
  * Topic订阅
  * Topic发布
  * Service
  * Action
  * Timer
  * 生命周期管理
* ROS Adapter 持有 Core 对象。
* ROS消息进入后转换为 Core 数据结构。
* Core 输出结果转换为 ROS 消息发布。

目录结构：

include/module_manager_hub/

```
common/
core/
ros/
```

src/

```
common/
core/
ros/
```

重构要求：

1. 保持现有功能完全一致。
2. 不修改 Topic 名称。
3. 不修改 Message/Srv 定义。
4. 不修改 Launch 文件行为。
5. 不修改配置文件格式。
6. 不修改外部接口。

输出内容：

1. 给出新的目录结构。
2. 给出需要新增的 Core 类。
3. 给出需要保留的 ROS Node 类。
4. 给出每个类的职责划分。
5. 给出关键代码修改点。
6. 给出 CMakeLists 修改方案。
7. 给出迁移步骤。
8. 优先最小改动重构，不要过度设计。
