#pragma once
#include "SensorBundle.h"
#include "Transform.h"

class Preprocessor {
public:
    static bool process(SensorBundle& bundle) {
        // 空数据直接过滤
        if (!bundle.image && !bundle.lidar)
            return false;

        // 在这里可以做：
        // 图像去畸变 / 点云去噪 / 时间戳校正 / 格式归一化
        return true;
    }
};