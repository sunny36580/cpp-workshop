#pragma once

#include "common/config_loader.h"
#include "runtime/process/service_manager/service_manager.h"

#include <string>

namespace robot_runtime {

// ============================================================================
// ServiceLoader — 服务配置加载工具
// ============================================================================
class ServiceLoader {
public:
    static bool load_all(ServiceManager& sm,
                         const std::string& config_dir,
                         const std::string& services_yaml = "services.yaml") {
        return sm.load_config(services_yaml);
    }
};

} // namespace robot_runtime
