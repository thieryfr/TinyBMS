#include "hal/interfaces/ihal_watchdog.h"

#include <memory>

namespace hal {

class MockWatchdog : public IHalWatchdog {
public:
    Status configure(const WatchdogConfig& config) override {
        config_ = config;
        stats_ = {};
        enabled_ = true;
        return Status::Ok;
    }

    Status enable() override {
        enabled_ = true;
        return Status::Ok;
    }

    Status disable() override {
        enabled_ = false;
        return Status::Ok;
    }

    Status feed() override {
        if (!enabled_) {
            return Status::Error;
        }
        stats_.feed_count++;
        return Status::Ok;
    }

    WatchdogStats getStats() const override { return stats_; }

private:
    WatchdogConfig config_{};
    bool enabled_ = false;
    WatchdogStats stats_{};
};

std::unique_ptr<IHalWatchdog> createMockWatchdog() {
    return std::make_unique<MockWatchdog>();
}

} // namespace hal
