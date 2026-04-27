#pragma once
#include "Sensor.h"
#include "../core/Frame.h"
#include "../utils/Time.h"
#include "../utils/Logger.h"
#include <thread>
#include <random>

class CameraSensor : public Sensor<ImageFrame> {
public:
    void start(Callback cb) override {
        run_ = true;
        th_ = std::thread([this, cb]() {
            int id = 0;
            static std::default_random_engine e(std::random_device{}());
            std::uniform_int_distribution<> j(-10,10);

            while (run_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(33 + j(e)));
                if (id > 50 && id < 80) { LOG_WARN("相机掉线: 帧" << id++); continue; }
                
                auto f = std::make_shared<ImageFrame>();
                f->ts = now_ms();
                f->id = id++;
                cb(f);
            }
        });
    }

    ~CameraSensor() { run_=false; if(th_.joinable()) th_.join(); }
private:
    std::thread th_;
    bool run_ = false;
};