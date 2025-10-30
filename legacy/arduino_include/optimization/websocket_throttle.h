#pragma once

#include <cstddef>
#include <cstdint>

namespace optimization {

struct WebsocketThrottleConfig {
    uint32_t min_interval_ms = 100;
    uint32_t burst_window_ms = 1000;
    uint32_t max_burst_count = 5;
    size_t max_payload_bytes = 4096;
};

class WebsocketThrottle {
public:
    WebsocketThrottle();
    explicit WebsocketThrottle(const WebsocketThrottleConfig& config);

    void configure(const WebsocketThrottleConfig& config);
    void reset();

    bool shouldSend(uint32_t now_ms, size_t payload_bytes) const;
    void recordSend(uint32_t now_ms, size_t payload_bytes);
    void recordDrop();

    uint32_t lastSendMs() const;
    uint32_t droppedCount() const;
    const WebsocketThrottleConfig& config() const;

private:
    WebsocketThrottleConfig config_{};
    uint32_t last_send_ms_ = 0;
    uint32_t dropped_count_ = 0;
    uint32_t window_start_ms_ = 0;
    uint32_t window_send_count_ = 0;
    bool has_sent_ = false;
};

}  // namespace optimization
