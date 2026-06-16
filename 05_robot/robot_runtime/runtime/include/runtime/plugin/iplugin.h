#pragma once

#include <string>

namespace robot_runtime {

// ============================================================================
// IPlugin — 插件接口（Phase6 启用）
// ============================================================================
// 所有动态库插件必须实现此接口。
// 现阶段仅预留接口声明，不做实现。
// ============================================================================
class IPlugin {
public:
    virtual ~IPlugin() = default;

    virtual std::string name() const = 0;
    virtual bool on_load() = 0;
    virtual bool on_unload() = 0;
};

} // namespace robot_runtime
