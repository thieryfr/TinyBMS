#pragma once

#include <cstdint>

namespace optimization {

struct AdaptivePollingConfig {
    uint32_t base_interval_ms = 100;
    uint32_t min_interval_ms = 50;
    uint32_t max_interval_ms = 500;
    uint32_t backoff_step_ms = 25;
    uint32_t recovery_step_ms = 10;
    uint32_t latency_target_ms = 40;
    uint32_t latency_slack_ms = 15;
    uint8_t failure_threshold = 3;
    uint8_t success_threshold = 6;
};

class AdaptivePoller {
public:
    AdaptivePoller();
    explicit AdaptivePoller(const AdaptivePollingConfig& config);

    void configure(const AdaptivePollingConfig& config);

    uint32_t currentInterval() const;
    uint32_t lastLatencyMs() const;
    uint32_t maxLatencyMs() const;
    float averageLatencyMs() const;
    uint32_t consecutiveFailures() const;
    uint32_t consecutiveSuccesses() const;

    void recordSuccess(uint32_t latency_ms, uint32_t bytes_transferred = 0);
    void recordFailure(uint32_t latency_ms);
    void recordTimeout();

private:
    void clampInterval();
    void backoff(uint32_t latency_ms);
    void recover(uint32_t latency_ms);

    AdaptivePollingConfig config_{};
    uint32_t interval_ms_ = 100;
    uint32_t last_latency_ms_ = 0;
    uint32_t max_latency_ms_ = 0;
    uint64_t latency_accumulator_ms_ = 0;
    uint32_t latency_samples_ = 0;
    uint32_t failure_streak_ = 0;
    uint32_t success_streak_ = 0;
};

}  // namespace optimization
