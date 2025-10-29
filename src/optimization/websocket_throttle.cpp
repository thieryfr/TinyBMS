#include "optimization/websocket_throttle.h"

#include <algorithm>

namespace optimization {

WebsocketThrottle::WebsocketThrottle() {
    configure(config_);
}

WebsocketThrottle::WebsocketThrottle(const WebsocketThrottleConfig& config) {
    configure(config);
}

void WebsocketThrottle::configure(const WebsocketThrottleConfig& config) {
    config_ = config;
    if (config_.min_interval_ms == 0) {
        config_.min_interval_ms = 1;
    }
    if (config_.burst_window_ms == 0) {
        config_.burst_window_ms = 1;
    }
    if (config_.max_burst_count == 0) {
        config_.max_burst_count = 1;
    }
    reset();
}

void WebsocketThrottle::reset() {
    last_send_ms_ = 0;
    dropped_count_ = 0;
    window_start_ms_ = 0;
    window_send_count_ = 0;
    has_sent_ = false;
}

bool WebsocketThrottle::shouldSend(uint32_t now_ms, size_t payload_bytes) const {
    if (payload_bytes > config_.max_payload_bytes) {
        return false;
    }

    if (!has_sent_) {
        return true;
    }

    const uint32_t elapsed_since_last = now_ms - last_send_ms_;
    if (elapsed_since_last < config_.min_interval_ms) {
        return false;
    }

    if (window_start_ms_ == 0) {
        return true;
    }

    uint32_t window_elapsed = now_ms - window_start_ms_;
    if (window_elapsed >= config_.burst_window_ms) {
        return true;
    }

    if (window_send_count_ >= config_.max_burst_count) {
        return false;
    }

    return true;
}

void WebsocketThrottle::recordSend(uint32_t now_ms, size_t /*payload_bytes*/) {
    if (window_start_ms_ == 0 || (now_ms - window_start_ms_) >= config_.burst_window_ms) {
        window_start_ms_ = now_ms;
        window_send_count_ = 0;
    }

    last_send_ms_ = now_ms;
    window_send_count_++;
    has_sent_ = true;
}

void WebsocketThrottle::recordDrop() {
    dropped_count_++;
}

uint32_t WebsocketThrottle::lastSendMs() const {
    return last_send_ms_;
}

uint32_t WebsocketThrottle::droppedCount() const {
    return dropped_count_;
}

const WebsocketThrottleConfig& WebsocketThrottle::config() const {
    return config_;
}

}  // namespace optimization
