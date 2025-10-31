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
    : initialized_(false)
    , configured_(false)
    , enabled_(false)
    , config_{}
    , stats_{}
    , last_feed_time_(0) {
}

ESP32WatchdogIDF::~ESP32WatchdogIDF() {
    if (enabled_) {
        disable();
    }
}

Status ESP32WatchdogIDF::configure(const WatchdogConfig& config) {
    // Check if already configured with same config (idempotent)
    if (configured_) {
        bool config_changed = (config_.timeout_ms != config.timeout_ms);

        if (!config_changed) {
            ESP_LOGD(TAG, "Watchdog already configured with same timeout, skipping");
            return Status::Ok;
        }

        // Config changed, need to reconfigure
        ESP_LOGI(TAG, "Watchdog config changed, reconfiguring...");
    }

    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = config.timeout_ms,
        .idle_core_mask = 0,  // Don't monitor idle tasks by default
        .trigger_panic = true  // Panic on timeout
    };

    esp_err_t err;

    if (!initialized_) {
        // First time: initialize watchdog
        err = esp_task_wdt_init(&wdt_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Watchdog init failed: %s", esp_err_to_name(err));
            return Status::Error;
        }
        initialized_ = true;
        ESP_LOGI(TAG, "Watchdog initialized: timeout=%lums", config.timeout_ms);
    } else {
        // Already initialized: reconfigure
        err = esp_task_wdt_reconfigure(&wdt_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Watchdog reconfigure failed: %s", esp_err_to_name(err));
            return Status::Error;
        }
        ESP_LOGI(TAG, "Watchdog reconfigured: timeout=%lums", config.timeout_ms);
    }

    config_ = config;
    configured_ = true;

    return Status::Ok;
}

Status ESP32WatchdogIDF::enable() {
    if (!configured_) {
        ESP_LOGW(TAG, "Watchdog not configured");
        return Status::Error;
    }

    if (enabled_) {
        ESP_LOGD(TAG, "Watchdog already enabled for this task");
        return Status::Ok;
    }

    // Add current task to watchdog
    esp_err_t err = esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    if (err == ESP_ERR_INVALID_ARG) {
        ESP_LOGW(TAG, "Task already subscribed to watchdog, marking as enabled");
        // Task already added, not an error
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Watchdog add task failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    enabled_ = true;
    last_feed_time_ = esp_timer_get_time();

    ESP_LOGI(TAG, "Watchdog enabled for task '%s'", pcTaskGetName(NULL));
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
