#pragma once

// 坐标变换：把传感器局部坐标 → 世界坐标
class Transform {
public:
    static float toWorldX(float x) {
        return x;
    }

    static float toWorldY(float y) {
        return y;
    }
};