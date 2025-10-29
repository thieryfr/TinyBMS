#include "hal/interfaces/ihal_timer.h"

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>
#include <memory>
#include <utility>

namespace hal {

namespace {

void timerCallbackStatic(TimerHandle_t handle) {
    auto context = static_cast<TimerContext*>(pvTimerGetTimerID(handle));
    if (!context) {
        return;
    }
    auto* callback = reinterpret_cast<TimerCallback*>(context->user);
    if (callback && *callback) {
        (*callback)(*context);
    }
}

class Esp32Timer : public IHalTimer {
public:
    ~Esp32Timer() override {
        stop();
    }

    Status start(const TimerConfig& config, TimerCallback callback, TimerContext context) override {
        stop();
        callback_ = std::move(callback);
        context_ = context;
        context_.user = &callback_;

        timer_ = xTimerCreate("hal", pdMS_TO_TICKS(config.period_ms), config.auto_reload ? pdTRUE : pdFALSE, &context_, timerCallbackStatic);
        if (!timer_) {
            return Status::Error;
        }
        if (xTimerStart(timer_, 0) != pdPASS) {
            xTimerDelete(timer_, 0);
            timer_ = nullptr;
            return Status::Error;
        }
        return Status::Ok;
    }

    Status stop() override {
        if (timer_) {
            xTimerStop(timer_, 0);
            xTimerDelete(timer_, 0);
            timer_ = nullptr;
        }
        callback_ = nullptr;
        return Status::Ok;
    }

    bool isActive() const override {
        return timer_ != nullptr;
    }

private:
    TimerHandle_t timer_ = nullptr;
    TimerCallback callback_;
    TimerContext context_{};
};

} // namespace

std::unique_ptr<IHalTimer> createEsp32Timer() {
    return std::make_unique<Esp32Timer>();
}

} // namespace hal
