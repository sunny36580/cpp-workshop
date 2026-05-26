#pragma once

#include "aimrt_module_cpp_interface/module_base.h"

namespace Logger::executor_co_loop_module {

class ExecutorCoLoopModule : public aimrt::ModuleBase {
 public:
  ExecutorCoLoopModule() = default;
  ~ExecutorCoLoopModule() override = default;

  aimrt::ModuleInfo Info() const override {
    return aimrt::ModuleInfo{.name = "ExecutorCoLoopModule"};
  }

  bool Initialize(aimrt::CoreRef core) override;

  bool Start() override;

  void Shutdown() override;

 private:
  auto GetLogger() { return core_.GetLogger(); }

 private:
  aimrt::CoreRef core_;
};

}  // namespace Logger::executor_co_loop_module
