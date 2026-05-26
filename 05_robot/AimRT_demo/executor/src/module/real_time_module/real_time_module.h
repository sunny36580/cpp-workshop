#pragma once

#include "aimrt_module_cpp_interface/module_base.h"

namespace Logger::real_time_module {

class RealTimeModule : public aimrt::ModuleBase {
 public:
  RealTimeModule() = default;
  ~RealTimeModule() override = default;

  aimrt::ModuleInfo Info() const override {
    return aimrt::ModuleInfo{.name = "RealTimeModule"};
  }

  bool Initialize(aimrt::CoreRef core) override;

  bool Start() override;

  void Shutdown() override;

 private:
  auto GetLogger() { return core_.GetLogger(); }

 private:
  aimrt::CoreRef core_;
};

}  // namespace Logger::real_time_module
