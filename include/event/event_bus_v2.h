#pragma once

#include <Arduino.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "event/event_subscriber.h"
#include "event/event_types_v2.h"

namespace tinybms::event {

struct BusStatistics {
    uint32_t total_published = 0;
    uint32_t total_delivered = 0;
    size_t subscriber_count = 0;
};

namespace detail {

template <typename T, typename = void>
struct has_metadata : std::false_type {};

template <typename T>
struct has_metadata<T, std::void_t<decltype(std::declval<T&>().metadata)>> : std::true_type {};

} // namespace detail

class EventBusV2 {
public:
    EventBusV2();

    template <typename Event>
    void publish(Event event);

    template <typename Event>
    EventSubscriber subscribe(std::function<void(const Event&)> callback);

    template <typename Event>
    bool getLatest(Event& out) const;

    template <typename Event>
    bool hasLatest() const;

    void resetStats();
    BusStatistics statistics() const;
    size_t subscriberCount() const { return subscriber_count_.load(std::memory_order_relaxed); }

private:
    template <typename Event>
    struct Channel {
        struct Subscription {
            std::function<void(const Event&)> callback;
        };

        mutable std::mutex mutex;
        std::vector<std::shared_ptr<Subscription>> subscribers;
        std::optional<Event> latest;
    };

    template <typename Event>
    Channel<Event>& channel() const;

    template <typename Event>
    static void fillMetadata(Event& event, std::true_type);

    template <typename Event>
    static void fillMetadata(Event&, std::false_type) {}

private:
    std::atomic<uint32_t> total_published_;
    std::atomic<uint32_t> total_delivered_;
    std::atomic<size_t> subscriber_count_;
    std::atomic<uint32_t> sequence_counter_;
};

extern EventBusV2 eventBus;

} // namespace tinybms::event

#include "event/event_bus_v2.tpp"
