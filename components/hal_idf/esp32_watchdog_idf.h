/**
 * @file esp32_watchdog_idf.h
 * @brief ESP-IDF native Watchdog HAL implementation header
 */

#pragma once

#include "hal/interfaces/ihal_watchdog.h"
#include "esp_task_wdt.h"

namespace hal {

class ESP32WatchdogIDF : public IHalWatchdog {
public:
    ESP32WatchdogIDF();
    ~ESP32WatchdogIDF() override;

    Status configure(const WatchdogConfig& config) override;
    Status enable() override;
    Status disable() override;
    Status feed() override;
    WatchdogStats getStats() const override;

private:
    bool initialized_;  // esp_task_wdt_init called
    bool configured_;
    bool enabled_;
    WatchdogConfig config_;  // Last configured settings
    WatchdogStats stats_;
    uint64_t last_feed_time_;
};

} // namespace hal
