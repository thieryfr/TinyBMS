/**
 * @file watchdog_manager.cpp
 * @brief Watchdog Manager implementation with FreeRTOS + Logging
 */

#include "watchdog_manager.h"
#include <Arduino.h>
#include "esp_task_wdt.h"
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "logger.h"
#include "config_manager.h"

extern Logger logger;
extern ConfigManager config;
extern SemaphoreHandle_t feedMutex;

WatchdogManager::WatchdogManager()
    : enabled_(false)
    , initialized_(false)
    , timeout_ms_(WATCHDOG_DEFAULT_TIMEOUT)
    , last_feed_time_(0)
    , init_time_(0)
    , feed_count_(0)
    , min_feed_interval_(0xFFFFFFFF)
    , max_feed_interval_(0)
    , total_feed_interval_(0)
    , reset_reason_(esp_reset_reason())
{
}

WatchdogManager::~WatchdogManager() {
    disable();
}

bool WatchdogManager::begin(uint32_t timeout_ms) {
    if (timeout_ms < WATCHDOG_MIN_TIMEOUT || timeout_ms > WATCHDOG_MAX_TIMEOUT) {
        logger.log(LOG_ERROR, "Watchdog begin failed: invalid timeout");
        return false;
    }

    timeout_ms_ = timeout_ms;
    init_time_ = millis();
    last_feed_time_ = init_time_;
    feed_count_ = 0;
    min_feed_interval_ = 0xFFFFFFFF;
    max_feed_interval_ = 0;
    total_feed_interval_ = 0;

    if (!configureHardware()) {
        logger.log(LOG_ERROR, "Hardware watchdog configuration failed");
        return false;
    }

    initialized_ = true;
    enabled_ = true;
    logger.log(LOG_INFO, "Watchdog initialized, timeout = " + String(timeout_ms_) + "ms");
    return true;
}

bool WatchdogManager::configureHardware() {
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = timeout_ms_,
        .idle_core_mask = 0,
        .trigger_panic = true
    };

    if (esp_task_wdt_init(&wdt_config) != ESP_OK) return false;
    if (esp_task_wdt_add(NULL) != ESP_OK) return false;

    return true;
}

bool WatchdogManager::disable() {
    if (!initialized_) return false;

    if (esp_task_wdt_delete(NULL) != ESP_OK) {
        logger.log(LOG_WARNING, "Failed to remove watchdog");
        return false;
    }

    enabled_ = false;
    logger.log(LOG_INFO, "Watchdog disabled");
    return true;
}

bool WatchdogManager::enable() {
    if (!initialized_) return false;

    if (esp_task_wdt_add(NULL) != ESP_OK) {
        logger.log(LOG_WARNING, "Failed to re-enable watchdog");
        return false;
    }

    enabled_ = true;
    esp_task_wdt_reset();
    last_feed_time_ = millis();
    logger.log(LOG_INFO, "Watchdog re-enabled");
    return true;
}

bool WatchdogManager::feed() {
    if (!initialized_ || !enabled_) return false;

    if (!validateFeedInterval()) {
        logger.log(LOG_DEBUG, "Watchdog feed ignored (too frequent)");
        return false;
    }

    unsigned long now = millis();
    uint32_t interval = now - last_feed_time_;

    if (esp_task_wdt_reset() != ESP_OK) {
        logger.log(LOG_ERROR, "Watchdog feed failed");
        return false;
    }

    updateStats(interval);
    last_feed_time_ = now;

    if (interval > timeout_ms_ * 0.9) {
        logger.log(LOG_WARNING, "Late watchdog feed (" + String(interval) + "ms)");
    }

    return true;
}

bool WatchdogManager::forceFeed() {
    if (!initialized_ || !enabled_) return false;

    unsigned long now = millis();
    uint32_t interval = now - last_feed_time_;

    if (esp_task_wdt_reset() != ESP_OK) {
        logger.log(LOG_ERROR, "Forced watchdog feed failed");
        return false;
    }

    updateStats(interval);
    last_feed_time_ = now;
    return true;
}

uint32_t WatchdogManager::getTimeSinceLastFeed() const {
    if (!initialized_) return 0;
    return millis() - last_feed_time_;
}

uint32_t WatchdogManager::getTimeUntilTimeout() const {
    if (!initialized_ || !enabled_) return 0;
    uint32_t elapsed = millis() - last_feed_time_;
    if (elapsed >= timeout_ms_) return 0;
    return timeout_ms_ - elapsed;
}

float WatchdogManager::getAverageFeedInterval() const {
    if (feed_count_ == 0) return 0.0f;
    return static_cast<float>(total_feed_interval_) / static_cast<float>(feed_count_);
}

bool WatchdogManager::validateFeedInterval() const {
    unsigned long now = millis();
    unsigned long interval = now - last_feed_time_;
    return interval >= WATCHDOG_MIN_FEED_INTERVAL;
}

void WatchdogManager::updateStats(uint32_t interval) {
    feed_count_++;

    if (interval < min_feed_interval_) min_feed_interval_ = interval;
    if (interval > max_feed_interval_) max_feed_interval_ = interval;
    total_feed_interval_ += interval;
}

bool WatchdogManager::checkHealth() const {
    if (!initialized_ || !enabled_) return true;
    return (millis() - last_feed_time_) < timeout_ms_;
}

String WatchdogManager::getResetReasonString() const {
    switch (reset_reason_) {
        case ESP_RST_UNKNOWN:  return "UNKNOWN";
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_EXT:      return "EXT";
        case ESP_RST_SW:       return "SW";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO:     return "SDIO";
        default:               return "OTHER";
    }
}

void WatchdogManager::printStats() const {
    logger.log(LOG_DEBUG,
        "WDT stats: count=" + String(feed_count_) +
        " min=" + String(min_feed_interval_) +
        " max=" + String(max_feed_interval_) +
        " avg=" + String(getAverageFeedInterval()) +
        " lastReset=" + getResetReasonString()
    );
}

void WatchdogManager::watchdogTask(void *pvParameters) {
    WatchdogManager *watchdog = static_cast<WatchdogManager *>(pvParameters);

    while (true) {
        if (watchdog->checkHealth()) {
            logger.log(LOG_DEBUG, "Watchdog: System healthy");
        } else {
            logger.log(LOG_WARNING, "Watchdog: ⚠️ System unhealthy");
        }

        watchdog->printStats();

        if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            watchdog->feed();
            xSemaphoreGive(feedMutex);
        }

        UBaseType_t stack = uxTaskGetStackHighWaterMark(NULL);
        logger.log(LOG_DEBUG, "watchdogTask stack: " + String(stack));

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
