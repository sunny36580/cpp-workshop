#pragma once

#include "aimrt_module_cpp_interface/module_base.h"

namespace Executor::executor_module {

// 执行器模块类，继承自模块基类
class ExecutorModule : public aimrt::ModuleBase {
 public:
  ExecutorModule() = default;
  ~ExecutorModule() override = default;

  // 获取模块信息
  aimrt::ModuleInfo Info() const override {
    return aimrt::ModuleInfo{.name = "ExecutorModule"};
  }  // 复制官方代码过来后需要添加命名空间  aimrt

  // 初始化模块
  bool Initialize(aimrt::CoreRef aimrt_ptr) override;

  // 启动模块
  bool Start() override;

  // 关闭模块
  void Shutdown() override;

 private:
  // 获取日志记录器
  auto GetLogger() { return core_.GetLogger(); }

  // 简单执行演示
  void SimpleExecuteDemo();
  // 线程安全演示
  void ThreadSafeDemo();
  // 时间调度演示
  void TimeScheduleDemo();

 private:
  aimrt::CoreRef core_;  // AIMRT框架核心引用

  aimrt::executor::ExecutorRef work_executor_;         // 普通执行器
  aimrt::executor::ExecutorRef thread_safe_executor_;  // 线程安全执行器

  std::atomic_bool run_flag_ = true;                     // 运行标志(原子变量)
  uint32_t loop_count_ = 0;                              // 循环计数器
  aimrt::executor::ExecutorRef time_schedule_executor_;  // 时间调度执行器
};

}  // namespace Executor::executor_module