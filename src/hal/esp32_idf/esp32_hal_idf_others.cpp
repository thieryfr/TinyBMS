/**
 * @file esp32_hal_idf_others.cpp
 * @brief ESP-IDF native GPIO, Timer, Watchdog HAL for PlatformIO
 *
 * Phase 2: Migration Périphériques
 */

#include "hal/interfaces/ihal_gpio.h"
#include "hal/interfaces/ihal_timer.h"
#include "hal/interfaces/ihal_watchdog.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <memory>

namespace hal {

// ============================================================================
// GPIO Implementation
// ============================================================================

static const char* GPIO_TAG = "ESP32GpioIDF";

class ESP32GpioIDF : public IHalGpio {
public:
    ESP32GpioIDF() : pin_(-1), configured_(false) {}

    Status configure(const GpioConfig& config) override {
        if (config.pin < 0 || config.pin > 39) {
            return Status::InvalidArgument;
        }

        pin_ = config.pin;

        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << pin_);

        switch (config.mode) {
            case GpioMode::Input:
                io_conf.mode = GPIO_MODE_INPUT;
                break;
            case GpioMode::Output:
                io_conf.mode = GPIO_MODE_OUTPUT;
                break;
            case GpioMode::InputPullUp:
                io_conf.mode = GPIO_MODE_INPUT;
                io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
                break;
            case GpioMode::InputPullDown:
                io_conf.mode = GPIO_MODE_INPUT;
                io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
                break;
            case GpioMode::OpenDrain:
                io_conf.mode = GPIO_MODE_OUTPUT_OD;
                break;
            default:
                return Status::InvalidArgument;
        }

        io_conf.intr_type = GPIO_INTR_DISABLE;

        if (gpio_config(&io_conf) != ESP_OK) {
            return Status::Error;
        }

        if (config.mode == GpioMode::Output || config.mode == GpioMode::OpenDrain) {
            gpio_set_level(static_cast<gpio_num_t>(pin_),
                          config.initial_level == GpioLevel::High ? 1 : 0);
        }

        configured_ = true;
        return Status::Ok;
    }

    Status write(GpioLevel level) override {
        if (!configured_) return Status::Error;
        return (gpio_set_level(static_cast<gpio_num_t>(pin_),
                              level == GpioLevel::High ? 1 : 0) == ESP_OK)
            ? Status::Ok : Status::Error;
    }

    GpioLevel read() override {
        if (!configured_) return GpioLevel::Low;
        return (gpio_get_level(static_cast<gpio_num_t>(pin_)) == 1)
            ? GpioLevel::High : GpioLevel::Low;
    }

private:
    int pin_;
    bool configured_;
};

// ============================================================================
// Timer Implementation
// ============================================================================

static const char* TIMER_TAG = "ESP32TimerIDF";

class ESP32TimerIDF : public IHalTimer {
public:
    ESP32TimerIDF() : timer_handle_(nullptr), callback_(nullptr), context_{}, active_(false) {}

    ~ESP32TimerIDF() override {
        stop();
        if (timer_handle_) {
            esp_timer_delete(timer_handle_);
        }
    }

    Status start(const TimerConfig& config, TimerCallback callback, TimerContext context) override {
        if (!callback) return Status::InvalidArgument;

        if (timer_handle_) {
            stop();
            esp_timer_delete(timer_handle_);
            timer_handle_ = nullptr;
        }

        callback_ = callback;
        context_ = context;

        esp_timer_create_args_t timer_args = {
            .callback = &timerCallbackStatic,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "hal_timer",
            .skip_unhandled_events = false
        };

        if (esp_timer_create(&timer_args, &timer_handle_) != ESP_OK) {
            return Status::Error;
        }

        uint64_t period_us = config.period_ms * 1000ULL;
        esp_err_t err = config.auto_reload
            ? esp_timer_start_periodic(timer_handle_, period_us)
            : esp_timer_start_once(timer_handle_, period_us);

        if (err != ESP_OK) {
            esp_timer_delete(timer_handle_);
            timer_handle_ = nullptr;
            return Status::Error;
        }

        active_ = true;
        return Status::Ok;
    }

    Status stop() override {
        if (!timer_handle_ || !active_) return Status::Ok;
        esp_timer_stop(timer_handle_);
        active_ = false;
        return Status::Ok;
    }

    bool isActive() const override {
        return active_ && timer_handle_ && esp_timer_is_active(timer_handle_);
    }

private:
    esp_timer_handle_t timer_handle_;
    TimerCallback callback_;
    TimerContext context_;
    bool active_;

    static void timerCallbackStatic(void* arg) {
        ESP32TimerIDF* self = static_cast<ESP32TimerIDF*>(arg);
        if (self && self->callback_) {
            self->callback_(self->context_);
        }
    }
};

// ============================================================================
// Watchdog Implementation
// ============================================================================

static const char* WDT_TAG = "ESP32WatchdogIDF";

class ESP32WatchdogIDF : public IHalWatchdog {
public:
    ESP32WatchdogIDF()
        : configured_(false)
        , enabled_(false)
        , stats_{}
        , last_feed_time_(0) {}

    ~ESP32WatchdogIDF() override {
        if (enabled_) {
            disable();
        }
    }

    Status configure(const WatchdogConfig& config) override {
        esp_task_wdt_config_t wdt_config = {
            .timeout_ms = config.timeout_ms,
            .idle_core_mask = 0,
            .trigger_panic = true
        };

        esp_err_t err;
        if (!configured_) {
            err = esp_task_wdt_init(&wdt_config);
        } else {
            err = esp_task_wdt_reconfigure(&wdt_config);
        }

        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return Status::Error;
        }

        configured_ = true;
        return Status::Ok;
    }

    Status enable() override {
        if (!configured_) {
            return Status::Error;
        }

        TaskHandle_t handle = xTaskGetCurrentTaskHandle();
        esp_err_t err = esp_task_wdt_add(handle);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return Status::Error;
        }

        enabled_ = true;
        last_feed_time_ = esp_timer_get_time();
        return Status::Ok;
    }

    Status disable() override {
        if (!configured_) {
            return Status::Ok;
        }

        TaskHandle_t handle = xTaskGetCurrentTaskHandle();
        esp_err_t err = esp_task_wdt_delete(handle);
        if (err != ESP_OK && err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_STATE) {
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

        uint64_t now = esp_timer_get_time();
        uint32_t interval_ms = static_cast<uint32_t>((now - last_feed_time_) / 1000);

        if (stats_.feed_count == 0) {
            stats_.min_interval_ms = interval_ms;
            stats_.max_interval_ms = interval_ms;
            stats_.average_interval_ms = interval_ms;
        } else {
            if (interval_ms < stats_.min_interval_ms) stats_.min_interval_ms = interval_ms;
            if (interval_ms > stats_.max_interval_ms) stats_.max_interval_ms = interval_ms;
            stats_.average_interval_ms =
                (stats_.average_interval_ms * stats_.feed_count + interval_ms) /
                (stats_.feed_count + 1);
        }

        stats_.feed_count++;
        last_feed_time_ = now;

        return Status::Ok;
    }

    WatchdogStats getStats() const override {
        return stats_;
    }

private:
    bool configured_;
    bool enabled_;
    WatchdogStats stats_;
    uint64_t last_feed_time_;
};

// ============================================================================
// Factory Functions
// ============================================================================

std::unique_ptr<IHalGpio> createEsp32IdfGpio() {
    return std::make_unique<ESP32GpioIDF>();
}

std::unique_ptr<IHalTimer> createEsp32IdfTimer() {
    return std::make_unique<ESP32TimerIDF>();
}

std::unique_ptr<IHalWatchdog> createEsp32IdfWatchdog() {
    return std::make_unique<ESP32WatchdogIDF>();
}

} // namespace hal
