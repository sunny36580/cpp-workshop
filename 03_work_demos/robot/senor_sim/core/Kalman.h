#pragma once

// 匀速模型卡尔曼滤波（极简工程版，适配无人机/传感器掉线补偿）
class KalmanFilter {
public:
    float x = 0.0f;   // X坐标
    float y = 0.0f;   // Y坐标
    float vx = 0.0f;  // X速度
    float vy = 0.0f;  // Y速度

    // 卡尔曼预测：传感器掉线时，靠速度推算当前位置
    void predict(float dt) {
        x += vx * dt;
        y += vy * dt;
    }

    // 卡尔曼更新：有传感器数据时，修正状态
    void update(float meas_x, float meas_y) {
        const float alpha = 0.6f;  // 滤波系数
        // 更新位置
        x = alpha * meas_x + (1 - alpha) * x;
        y = alpha * meas_y + (1 - alpha) * y;
        // 更新速度
        vx = alpha * (meas_x - x) / 0.02f + (1 - alpha) * vx;
        vy = alpha * (meas_y - y) / 0.02f + (1 - alpha) * vy;
    }
};