#include <cassert>
#include <cstdint>
#include "optimization/adaptive_polling.h"
#include "optimization/ring_buffer.h"
#include "optimization/websocket_throttle.h"

using namespace optimization;

int main() {
    {
        AdaptivePollingConfig config{};
        config.base_interval_ms = 100;
        config.min_interval_ms = 50;
        config.max_interval_ms = 200;
        config.backoff_step_ms = 20;
        config.recovery_step_ms = 10;
        config.latency_target_ms = 40;
        config.latency_slack_ms = 10;
        config.failure_threshold = 2;
        config.success_threshold = 3;

        AdaptivePoller poller(config);
        assert(poller.currentInterval() == 100);

        poller.recordSuccess(35);
        poller.recordSuccess(30);
        poller.recordSuccess(32);
        assert(poller.currentInterval() <= 100);

        poller.recordFailure(100);
        assert(poller.currentInterval() >= 100);
        poller.recordTimeout();
        assert(poller.currentInterval() >= 100);
    }

    {
        ByteRingBuffer buffer(8);
        uint8_t data[4] = {1, 2, 3, 4};
        assert(buffer.push(data, 4) == 4);
        assert(buffer.size() == 4);
        uint8_t peeked[4] = {0};
        assert(buffer.peek(peeked, 4) == 4);
        for (int i = 0; i < 4; ++i) {
            assert(peeked[i] == static_cast<uint8_t>(i + 1));
        }
        uint8_t popped[4] = {0};
        assert(buffer.pop(popped, 4) == 4);
        for (int i = 0; i < 4; ++i) {
            assert(popped[i] == static_cast<uint8_t>(i + 1));
        }
        assert(buffer.empty());

        uint8_t fill[8] = {0};
        assert(buffer.push(fill, 8) == 8);
        assert(buffer.full());
        uint8_t overflow = 42;
        assert(buffer.push(&overflow, 1) == 0);
        buffer.clear();
        assert(buffer.empty());
    }

    {
        WebsocketThrottleConfig cfg{};
        cfg.min_interval_ms = 100;
        cfg.burst_window_ms = 500;
        cfg.max_burst_count = 2;
        cfg.max_payload_bytes = 128;

        WebsocketThrottle throttle(cfg);
        uint32_t now = 0;
        assert(throttle.shouldSend(now, 64));
        throttle.recordSend(now, 64);
        now += 50;
        assert(!throttle.shouldSend(now, 64));
        throttle.recordDrop();
        now += 100;
        assert(throttle.shouldSend(now, 64));
        throttle.recordSend(now, 64);
        now += 10;
        assert(!throttle.shouldSend(now, 64));
        throttle.recordDrop();
        assert(throttle.droppedCount() == 2);
    }

    return 0;
}
