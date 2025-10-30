/**
 * @file esp32_timer_idf.cpp
 * @brief ESP-IDF native Timer HAL implementation
 */

#include "esp32_timer_idf.h"
#include "esp_log.h"

namespace hal {

static const char* TAG = "ESP32TimerIDF";

ESP32TimerIDF::ESP32TimerIDF()
    : timer_handle_(nullptr)
    , callback_(nullptr)
    , context_{}
    , active_(false) {
}

ESP32TimerIDF::~ESP32TimerIDF() {
    stop();
    if (timer_handle_) {
        esp_timer_delete(timer_handle_);
    }
}

void ESP32TimerIDF::timerCallbackStatic(void* arg) {
    ESP32TimerIDF* self = static_cast<ESP32TimerIDF*>(arg);
    if (self && self->callback_) {
        self->callback_(self->context_);
    }
}

Status ESP32TimerIDF::start(const TimerConfig& config, TimerCallback callback, TimerContext context) {
    if (!callback) {
        return Status::InvalidArgument;
    }

    // Stop existing timer if any
    if (timer_handle_) {
        stop();
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }

    callback_ = callback;
    context_ = context;

    // Create timer
    esp_timer_create_args_t timer_args = {
        .callback = &timerCallbackStatic,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "hal_timer",
        .skip_unhandled_events = false
    };

    esp_err_t err = esp_timer_create(&timer_args, &timer_handle_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer create failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    // Start timer
    uint64_t period_us = config.period_ms * 1000ULL;
    if (config.auto_reload) {
        err = esp_timer_start_periodic(timer_handle_, period_us);
    } else {
        err = esp_timer_start_once(timer_handle_, period_us);
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Timer start failed: %s", esp_err_to_name(err));
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
        return Status::Error;
    }

    active_ = true;
    ESP_LOGI(TAG, "Timer started: period=%lums, auto_reload=%d",
             config.period_ms, config.auto_reload);

    return Status::Ok;
}

Status ESP32TimerIDF::stop() {
    if (!timer_handle_ || !active_) {
        return Status::Ok;
    }

    esp_err_t err = esp_timer_stop(timer_handle_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Timer stop failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    active_ = false;
    return Status::Ok;
}

bool ESP32TimerIDF::isActive() const {
    return active_ && timer_handle_ && esp_timer_is_active(timer_handle_);
}

} // namespace hal
