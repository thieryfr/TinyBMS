#include "hal/interfaces/ihal_timer.h"

#include <memory>
#include <utility>

namespace hal {

class MockTimer : public IHalTimer {
public:
    Status start(const TimerConfig& config, TimerCallback callback, TimerContext context) override {
        config_ = config;
        callback_ = std::move(callback);
        context_ = context;
        active_ = true;
        return Status::Ok;
    }

    Status stop() override {
        active_ = false;
        callback_ = nullptr;
        return Status::Ok;
    }

    bool isActive() const override { return active_; }

    void trigger() {
        if (callback_) {
            callback_(context_);
        }
    }

private:
    TimerConfig config_{};
    TimerCallback callback_;
    TimerContext context_{};
    bool active_ = false;
};

std::unique_ptr<IHalTimer> createMockTimer() {
    return std::make_unique<MockTimer>();
}

} // namespace hal
