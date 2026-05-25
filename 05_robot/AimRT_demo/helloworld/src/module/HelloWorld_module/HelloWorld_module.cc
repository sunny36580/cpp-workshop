#include "HelloWorld_module/HelloWorld_module.h"

#include "yaml-cpp/yaml.h"

namespace helloworld::HelloWorld_module {

bool HelloworldModule::Initialize(aimrt::CoreRef core) {
  // Save aimrt framework handle
  core_ = core;

  try {
    // Read cfg
    auto file_path = core_.GetConfigurator().GetConfigFilePath();
    if (!file_path.empty()) {
      YAML::Node cfg_node = YAML::LoadFile(std::string(file_path));
      for (const auto& itr : cfg_node) {
        std::string k = itr.first.as<std::string>();
        std::string v = itr.second.as<std::string>();
        AIMRT_INFO("cfg [{} : {}]", k, v);
      }
    }

  } catch (const std::exception& e) {
    AIMRT_ERROR("Init failed, {}", e.what());
    return false;
  }

  AIMRT_INFO("Init succeeded.");

  return true;
}

bool HelloworldModule::Start() {
  AIMRT_INFO("HelloWorld_module Started succeeded.");
  return true;
}

void HelloworldModule::Shutdown() {
  AIMRT_INFO("HelloWorld_module Shutdown succeeded.");
}

}  // namespace helloworld::HelloWorld_module
