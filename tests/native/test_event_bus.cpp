#include <cassert>
#include <cmath>
#include <Arduino.h>
#include "event_bus.h"

namespace {
struct CallbackContext {
    int call_count = 0;
};

void liveDataCallback(const BusEvent& event, void* user_data) {
    auto* ctx = static_cast<CallbackContext*>(user_data);
    assert(ctx != nullptr);
    ctx->call_count++;
    assert(event.type == EVENT_LIVE_DATA_UPDATE);
}
}

int main() {
    EventBus& bus = EventBus::getInstance();

    bool initialized = bus.begin(2);
    assert(initialized);

    bus.resetStats();
    arduino_stub::resetMillis();

    TinyBMS_LiveData first{};
    first.voltage = 50.5f;
    first.current = -12.0f;
    first.soc_percent = 68.0f;

    TinyBMS_LiveData second{};
    second.voltage = 51.2f;
    second.current = -10.5f;
    second.soc_percent = 70.5f;

    TinyBMS_LiveData third{};
    third.voltage = 49.8f;
    third.current = -9.0f;
    third.soc_percent = 65.0f;

    arduino_stub::advanceMillis(5);
    bool published_first = bus.publishLiveData(first, SOURCE_ID_UART);
    assert(published_first);

    arduino_stub::advanceMillis(5);
    bool published_second = bus.publishLiveData(second, SOURCE_ID_UART);
    assert(published_second);

    arduino_stub::advanceMillis(5);
    bool published_third = bus.publishLiveData(third, SOURCE_ID_UART);
    assert(!published_third);

    BusEvent latest{};
    bool has_latest = bus.getLatest(EVENT_LIVE_DATA_UPDATE, latest);
    assert(has_latest);
    assert(std::fabs(latest.data.live_data.voltage - second.voltage) < 1e-5f);
    assert(std::fabs(latest.data.live_data.current - second.current) < 1e-5f);
    assert(std::fabs(latest.data.live_data.soc_percent - second.soc_percent) < 1e-5f);
    assert(latest.source_id == SOURCE_ID_UART);
    assert(latest.sequence_number == 1);
    assert(latest.timestamp_ms == 10);
    assert(bus.hasLatest(EVENT_LIVE_DATA_UPDATE));

    EventBus::BusStats stats{};
    bus.getStats(stats);
    assert(stats.total_events_published == 2);
    assert(stats.queue_overruns == 1);
    assert(stats.total_events_dispatched == 0);
    assert(stats.total_subscribers == 0);
    assert(stats.current_queue_depth == 2);

    bus.resetStats();
    bus.getStats(stats);
    assert(stats.total_events_published == 0);
    assert(stats.total_events_dispatched == 0);
    assert(stats.queue_overruns == 0);
    assert(stats.total_subscribers == 0);
    assert(stats.current_queue_depth == 2);

    CallbackContext ctx{};
    bool subscribed = bus.subscribe(EVENT_LIVE_DATA_UPDATE, liveDataCallback, &ctx);
    assert(subscribed);
    bus.getStats(stats);
    assert(stats.total_subscribers == 1);
    assert(bus.getSubscriberCount(EVENT_LIVE_DATA_UPDATE) == 1);
    assert(ctx.call_count == 0);

    return 0;
}

