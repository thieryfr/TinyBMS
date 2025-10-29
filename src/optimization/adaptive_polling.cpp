#include "optimization/adaptive_polling.h"

#include <algorithm>

namespace optimization {

namespace {
constexpr uint32_t kMinLatencyTarget = 5;
constexpr uint32_t kMinInterval = 5;
}

AdaptivePoller::AdaptivePoller() {
    configure(config_);
}

AdaptivePoller::AdaptivePoller(const AdaptivePollingConfig& config) {
    configure(config);
}

void AdaptivePoller::configure(const AdaptivePollingConfig& config) {
    config_ = config;
    config_.min_interval_ms = std::max(config_.min_interval_ms, kMinInterval);
    config_.max_interval_ms = std::max(config_.max_interval_ms, config_.min_interval_ms);
    config_.latency_target_ms = std::max(config_.latency_target_ms, kMinLatencyTarget);
    interval_ms_ = std::clamp(config_.base_interval_ms, config_.min_interval_ms, config_.max_interval_ms);
    last_latency_ms_ = 0;
    max_latency_ms_ = 0;
    latency_accumulator_ms_ = 0;
    latency_samples_ = 0;
    failure_streak_ = 0;
    success_streak_ = 0;
}

uint32_t AdaptivePoller::currentInterval() const {
    return interval_ms_;
}

uint32_t AdaptivePoller::lastLatencyMs() const {
    return last_latency_ms_;
}

uint32_t AdaptivePoller::maxLatencyMs() const {
    return max_latency_ms_;
}

float AdaptivePoller::averageLatencyMs() const {
    if (latency_samples_ == 0) {
        return 0.0f;
    }
    return static_cast<float>(latency_accumulator_ms_) / static_cast<float>(latency_samples_);
}

uint32_t AdaptivePoller::consecutiveFailures() const {
    return failure_streak_;
}

uint32_t AdaptivePoller::consecutiveSuccesses() const {
    return success_streak_;
}

void AdaptivePoller::recordSuccess(uint32_t latency_ms, uint32_t /*bytes_transferred*/) {
    last_latency_ms_ = latency_ms;
    max_latency_ms_ = std::max(max_latency_ms_, latency_ms);
    latency_accumulator_ms_ += latency_ms;
    latency_samples_++;

    if (failure_streak_ > 0) {
        failure_streak_ = 0;
    }
    success_streak_++;

    recover(latency_ms);
    clampInterval();
}

void AdaptivePoller::recordFailure(uint32_t latency_ms) {
    last_latency_ms_ = latency_ms;
    max_latency_ms_ = std::max(max_latency_ms_, latency_ms);
    latency_accumulator_ms_ += latency_ms;
    latency_samples_++;

    success_streak_ = 0;
    failure_streak_++;

    backoff(latency_ms);
    clampInterval();
}

void AdaptivePoller::recordTimeout() {
    recordFailure(config_.latency_target_ms + config_.latency_slack_ms);
}

void AdaptivePoller::clampInterval() {
    if (interval_ms_ < config_.min_interval_ms) {
        interval_ms_ = config_.min_interval_ms;
    }
    if (interval_ms_ > config_.max_interval_ms) {
        interval_ms_ = config_.max_interval_ms;
    }
}

void AdaptivePoller::backoff(uint32_t latency_ms) {
    const uint32_t slack_target = config_.latency_target_ms + config_.latency_slack_ms;
    if (latency_ms >= slack_target || failure_streak_ >= config_.failure_threshold) {
        uint32_t delta = config_.backoff_step_ms;
        if (latency_ms > slack_target) {
            const uint32_t overshoot = latency_ms - slack_target;
            delta += overshoot;
        }
        interval_ms_ = std::min(config_.max_interval_ms, interval_ms_ + delta);
        failure_streak_ = 0;
    }
}

void AdaptivePoller::recover(uint32_t latency_ms) {
    if (interval_ms_ <= config_.min_interval_ms) {
        return;
    }

    const uint32_t slack_target = config_.latency_target_ms + config_.latency_slack_ms;
    if (latency_ms <= slack_target && success_streak_ >= config_.success_threshold) {
        uint32_t delta = config_.recovery_step_ms;
        if (latency_ms + config_.latency_slack_ms < slack_target && interval_ms_ > config_.min_interval_ms) {
            delta += config_.recovery_step_ms;
        }
        if (interval_ms_ > delta) {
            interval_ms_ -= delta;
        } else {
            interval_ms_ = config_.min_interval_ms;
        }
        success_streak_ = 0;
    }
}

}  // namespace optimization
