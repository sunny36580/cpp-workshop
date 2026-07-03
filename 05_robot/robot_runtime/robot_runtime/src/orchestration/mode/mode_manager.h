/**
 * @file mode_manager.h
 * @brief 模式编排
 * @role orchestration
 */
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

#include "common/config_loader.h"

namespace robot_runtime {

class ServiceManager;

/**
 * @class ModeManager
 * @brief 模式编排
 * @responsibility 加载 modes.yaml，计算模式 diff 并执行启停
 */
class ModeManager {
public:
    /**
     * @brief 构造函数
     * @param config_dir 配置目录
     * @param sm 服务管理器
     */
    ModeManager(std::string config_dir, ServiceManager* sm);

    /**
     * @brief 加载模式配置文件
     * @param modes_yaml modes.yaml 文件名
     * @return true=加载成功
     */
    bool load_config(const std::string& modes_yaml);
    
    /**
     * @brief 切换至目标模式
     * @param mode_name 目标模式名
     * @return true=切换成功
     */
    bool switch_to(const std::string& mode_name);

    /**
     * @brief 应用默认模式
     */
    void apply_default();

    const std::string& current_mode() const { return current_mode_; }
    const std::string& default_mode() const { return default_mode_; }
    const auto& modes() const { return modes_; }

private:
    std::string config_dir_;                        // 配置目录
    ServiceManager* sm_ = nullptr;                  // 服务管理器
    std::unordered_map<std::string, std::vector<ModeServiceEntry>> modes_;
    std::string current_mode_;                       // 当前模式名
    std::string default_mode_ = "standby";           // 默认模式名
};

} // namespace robot_runtime
