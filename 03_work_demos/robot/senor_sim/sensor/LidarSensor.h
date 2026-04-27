#pragma once
#include "Sensor.h"
#include "../core/Frame.h"
#include "../utils/Time.h"
#include <thread>

class LidarSensor : public Sensor<LidarFrame> {
public:
    void start(Callback cb) override {
        run_ = true;
        th_ = std::thread([this, cb]() {
            int id = 0;
            while (run_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
                auto f = std::make_shared<LidarFrame>();
                f->ts = now_ms();
                f->id = id++;
                cb(f);
            }
        });
    }

    ~LidarSensor() { run_=false; if(th_.joinable()) th_.join(); }
private:
    std::thread th_;
    bool run_ = false;
};