#include "hal/interfaces/ihal_watchdog.h"

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <memory>

namespace hal {

class Esp32Watchdog : public IHalWatchdog {
public:
    Status configure(const WatchdogConfig& config) override {
        config_ = config;
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = config.timeout_ms,
            .idle_core_mask = 0,
            .trigger_panic = true,
        };
        if (esp_task_wdt_init(&wdt_config) != ESP_OK) {
            return Status::Error;
        }
        if (esp_task_wdt_add(nullptr) != ESP_OK) {
            return Status::Error;
        }
        last_feed_ = millis();
        stats_ = {};
        return Status::Ok;
    }

    Status enable() override {
        if (esp_task_wdt_add(nullptr) != ESP_OK) {
            return Status::Error;
        }
        enabled_ = true;
        last_feed_ = millis();
        return Status::Ok;
    }

    Status disable() override {
        if (esp_task_wdt_delete(nullptr) != ESP_OK) {
            return Status::Error;
        }
        enabled_ = false;
        return Status::Ok;
    }

    Status feed() override {
        if (!enabled_) {
            return Status::Error;
        }
        if (esp_task_wdt_reset() != ESP_OK) {
            return Status::Error;
        }
        auto now = millis();
        uint32_t interval = now - last_feed_;
        if (stats_.feed_count == 0 || interval < stats_.min_interval_ms) {
            stats_.min_interval_ms = interval;
        }
        if (interval > stats_.max_interval_ms) {
            stats_.max_interval_ms = interval;
        }
        stats_.feed_count++;
        total_interval_ += interval;
        stats_.average_interval_ms = stats_.feed_count > 0
            ? static_cast<float>(total_interval_) / static_cast<float>(stats_.feed_count)
            : 0.0f;
        last_feed_ = now;
        return Status::Ok;
    }

    WatchdogStats getStats() const override {
        return stats_;
    }

private:
    WatchdogConfig config_{};
    bool enabled_ = true;
    uint32_t last_feed_ = 0;
    uint64_t total_interval_ = 0;
    WatchdogStats stats_{};
};

std::unique_ptr<IHalWatchdog> createEsp32Watchdog() {
    return std::make_unique<Esp32Watchdog>();
}

} // namespace hal
