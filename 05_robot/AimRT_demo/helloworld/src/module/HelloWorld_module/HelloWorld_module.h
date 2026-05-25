#pragma once

#include "aimrt_module_cpp_interface/module_base.h"

namespace helloworld::HelloWorld_module {

class HelloworldModule : public aimrt::ModuleBase {
 public:
  HelloworldModule() = default;
  ~HelloworldModule() override = default;

  aimrt::ModuleInfo Info() const override {
    return aimrt::ModuleInfo{.name = "HelloworldModule"};
  }

  bool Initialize(aimrt::CoreRef core) override;

  bool Start() override;

  void Shutdown() override;

 private:
  auto GetLogger() { return core_.GetLogger(); }

 private:
  aimrt::CoreRef core_;
};

}  // namespace helloworld::HelloWorld_module
