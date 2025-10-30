/**
 * @file esp32_timer_idf.h
 * @brief ESP-IDF native Timer HAL implementation header
 */

#pragma once

#include "hal/interfaces/ihal_timer.h"
#include "esp_timer.h"

namespace hal {

class ESP32TimerIDF : public IHalTimer {
public:
    ESP32TimerIDF();
    ~ESP32TimerIDF() override;

    Status start(const TimerConfig& config, TimerCallback callback, TimerContext context) override;
    Status stop() override;
    bool isActive() const override;

private:
    esp_timer_handle_t timer_handle_;
    TimerCallback callback_;
    TimerContext context_;
    bool active_;

    static void timerCallbackStatic(void* arg);
};

} // namespace hal
