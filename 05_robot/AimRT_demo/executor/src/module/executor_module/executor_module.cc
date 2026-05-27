#include "executor_module/executor_module.h"

#include "yaml-cpp/yaml.h"

namespace Logger::executor_module {

// 初始化模块
bool ExecutorModule::Initialize(aimrt::CoreRef core) {
  // 保存AIMRT框架句柄
  core_ = core;

  // 获取工作执行器
  work_executor_ = core_.GetExecutorManager().GetExecutor("work_executor");
  AIMRT_CHECK_ERROR_THROW(work_executor_, "无法获取工作执行器(work_executor)");

  // 获取线程安全执行器
  thread_safe_executor_ =
      core_.GetExecutorManager().GetExecutor("thread_safe_executor");
  AIMRT_CHECK_ERROR_THROW(
      thread_safe_executor_ && thread_safe_executor_.ThreadSafe(),
      "无法获取线程安全执行器(thread_safe_executor)");

  // 获取时间调度执行器
  time_schedule_executor_ =
      core_.GetExecutorManager().GetExecutor("time_schedule_executor");
  AIMRT_CHECK_ERROR_THROW(
      time_schedule_executor_ && time_schedule_executor_.SupportTimerSchedule(),
      "无法获取时间调度执行器(time_schedule_executor)");

  AIMRT_INFO("初始化成功");

  return true;
}

// 启动模块
bool ExecutorModule::Start() {
  // 测试简单执行
  SimpleExecuteDemo();

  // 测试线程安全执行
  ThreadSafeDemo();

  // 测试时间调度执行
  TimeScheduleDemo();

  AIMRT_INFO("启动成功");

  return true;
}

// 任务函数实现

// 简单执行演示
void ExecutorModule::SimpleExecuteDemo() {
  work_executor_.Execute([this]() { AIMRT_INFO("这是一个简单任务"); });
}

// 线程安全演示
void ExecutorModule::ThreadSafeDemo() {
  uint32_t n = 0;
  for (uint32_t ii = 0; ii < 10000; ++ii) {
    thread_safe_executor_.Execute([&n]() { n++; });  // 线程安全执行器
    // work_executor_.Execute([&n]() { n++; }); // 普通执行器
  }

  std::this_thread::sleep_for(std::chrono::seconds(1));  // 等待1秒

  AIMRT_INFO("n的值为: {}", n);
}

// 时间调度演示
void ExecutorModule::TimeScheduleDemo() {
  if (!run_flag_) return;  // 检查运行标志

  AIMRT_INFO("循环计数: {}", loop_count_++);

  // 1秒后再次执行本函数
  time_schedule_executor_.ExecuteAfter(
      std::chrono::seconds(1),
      std::bind(&ExecutorModule::TimeScheduleDemo, this));
}

// 关闭模块
void ExecutorModule::Shutdown() {
  run_flag_ = false;  // 设置运行标志为false

  std::this_thread::sleep_for(std::chrono::seconds(1));  // 等待1秒

  AIMRT_INFO("关闭成功");
}

}  // namespace Logger::executor_module
