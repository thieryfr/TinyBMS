#include <Arduino.h>
#include <cassert>
#include <cmath>

#include "event/event_bus_v2.h"
#include "event/event_types_v2.h"

using tinybms::event::EventBusV2;
using tinybms::event::BusStatistics;
using tinybms::events::EventSource;
using tinybms::events::LiveDataUpdate;

int main() {
    EventBusV2 bus;
    bus.resetStats();
    arduino_stub::resetMillis();

    LiveDataUpdate update{};
    update.metadata.source = EventSource::Uart;
    update.data.voltage = 50.0f;
    update.data.current = -10.0f;

    bus.publish(update);

    LiveDataUpdate latest{};
    assert(bus.getLatest(latest));
    assert(std::fabs(latest.data.voltage - 50.0f) < 1e-6f);
    assert(std::fabs(latest.data.current + 10.0f) < 1e-6f);
    assert(latest.metadata.sequence == 0);

    bool callback_called = false;
    auto subscriber = bus.subscribe<LiveDataUpdate>([&](const LiveDataUpdate& evt) {
        callback_called = true;
        assert(evt.metadata.sequence >= 1);
        assert(evt.metadata.source == EventSource::Uart);
    });

    arduino_stub::advanceMillis(5);
    bus.publish(update);
    assert(callback_called);

    BusStatistics stats = bus.statistics();
    assert(stats.total_published == 2);
    assert(stats.total_delivered == 1);
    assert(stats.subscriber_count == 1);

    subscriber.unsubscribe();
    stats = bus.statistics();
    assert(stats.subscriber_count == 0);

    bus.resetStats();
    stats = bus.statistics();
    assert(stats.total_published == 0);
    assert(stats.total_delivered == 0);

    return 0;
}
