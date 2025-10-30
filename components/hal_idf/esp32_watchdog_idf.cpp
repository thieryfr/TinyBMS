/**
 * @file esp32_watchdog_idf.cpp
 * @brief ESP-IDF native Watchdog HAL implementation
 */

#include "esp32_watchdog_idf.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace hal {

static const char* TAG = "ESP32WatchdogIDF";

ESP32WatchdogIDF::ESP32WatchdogIDF()
    : configured_(false)
    , enabled_(false)
    , stats_{}
    , last_feed_time_(0) {
}

ESP32WatchdogIDF::~ESP32WatchdogIDF() {
    if (enabled_) {
        disable();
    }
}

Status ESP32WatchdogIDF::configure(const WatchdogConfig& config) {
    // ESP-IDF Task WDT is typically configured globally
    // For Phase 1, we'll just track the config and use esp_task_wdt APIs

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = config.timeout_ms,
        .idle_core_mask = 0,  // Don't monitor idle tasks by default
        .trigger_panic = true  // Panic on timeout
    };

    esp_err_t err = esp_task_wdt_reconfigure(&wdt_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Watchdog configure failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    configured_ = true;
    ESP_LOGI(TAG, "Watchdog configured: timeout=%lums", config.timeout_ms);

    return Status::Ok;
}

Status ESP32WatchdogIDF::enable() {
    if (!configured_) {
        ESP_LOGW(TAG, "Watchdog not configured");
        return Status::Error;
    }

    // Add current task to watchdog
    esp_err_t err = esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Watchdog add task failed: %s", esp_err_to_name(err));
        // Don't return error, might already be added
    }

    enabled_ = true;
    last_feed_time_ = esp_timer_get_time();

    ESP_LOGI(TAG, "Watchdog enabled");
    return Status::Ok;
}

Status ESP32WatchdogIDF::disable() {
    if (!enabled_) {
        return Status::Ok;
    }

    // Remove current task from watchdog
    esp_err_t err = esp_task_wdt_delete(xTaskGetCurrentTaskHandle());
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Watchdog delete task failed: %s", esp_err_to_name(err));
    }

    enabled_ = false;
    ESP_LOGI(TAG, "Watchdog disabled");

    return Status::Ok;
}

Status ESP32WatchdogIDF::feed() {
    if (!enabled_) {
        return Status::Error;
    }

    esp_err_t err = esp_task_wdt_reset();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Watchdog feed failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    // Update stats
    uint64_t now = esp_timer_get_time();
    uint64_t interval_us = now - last_feed_time_;
    uint32_t interval_ms = interval_us / 1000;

    if (stats_.feed_count == 0) {
        stats_.min_interval_ms = interval_ms;
        stats_.max_interval_ms = interval_ms;
        stats_.average_interval_ms = interval_ms;
    } else {
        if (interval_ms < stats_.min_interval_ms) {
            stats_.min_interval_ms = interval_ms;
        }
        if (interval_ms > stats_.max_interval_ms) {
            stats_.max_interval_ms = interval_ms;
        }

        // Running average
        stats_.average_interval_ms =
            (stats_.average_interval_ms * stats_.feed_count + interval_ms) /
            (stats_.feed_count + 1);
    }

    stats_.feed_count++;
    last_feed_time_ = now;

    return Status::Ok;
}

WatchdogStats ESP32WatchdogIDF::getStats() const {
    return stats_;
}

} // namespace hal
