#pragma once

#include <Arduino.h>
#include <esp_system.h>

class WatchdogManager {
public:
    WatchdogManager();
    ~WatchdogManager();

    bool begin(uint32_t timeout_ms);
    bool enable();
    bool disable();
    bool feed();
    bool forceFeed();

    bool isEnabled() const { return enabled_; }
    bool isInitialized() const { return initialized_; }
    uint32_t getTimeout() const { return timeout_ms_; }
    uint32_t getTimeSinceLastFeed() const;
    uint32_t getTimeUntilTimeout() const;
    uint32_t getFeedCount() const { return feed_count_; }
    float getAverageFeedInterval() const;
    bool checkHealth() const;
    String getResetReasonString() const;

    static void watchdogTask(void *pvParameters);

private:
    bool configureHardware();
    bool validateFeedInterval() const;
    void updateStats(uint32_t interval);
    void printStats() const;

private:
    bool enabled_;
    bool initialized_;
    uint32_t timeout_ms_;
    uint32_t last_feed_time_;
    uint32_t init_time_;
    uint32_t feed_count_;
    uint32_t min_feed_interval_;
    uint32_t max_feed_interval_;
    uint64_t total_feed_interval_;
    esp_reset_reason_t reset_reason_;
};
