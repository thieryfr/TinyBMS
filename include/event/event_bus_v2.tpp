#pragma once

#include <algorithm>

namespace tinybms::event {

inline EventBusV2::EventBusV2()
    : total_published_(0)
    , total_delivered_(0)
    , subscriber_count_(0)
    , sequence_counter_(0) {}

template <typename Event>
void EventBusV2::publish(Event event) {
    fillMetadata(event, detail::has_metadata<Event>{});

    auto& ch = channel<Event>();
    std::vector<std::shared_ptr<typename Channel<Event>::Subscription>> subscribers;
    {
        std::lock_guard<std::mutex> lock(ch.mutex);
        ch.latest = event;
        subscribers = ch.subscribers;
    }

    total_published_.fetch_add(1, std::memory_order_relaxed);
    uint32_t delivered = 0;

    for (const auto& sub : subscribers) {
        if (sub && sub->callback) {
            sub->callback(event);
            ++delivered;
        }
    }

    if (delivered > 0) {
        total_delivered_.fetch_add(delivered, std::memory_order_relaxed);
    }
}

template <typename Event>
EventSubscriber EventBusV2::subscribe(std::function<void(const Event&)> callback) {
    auto& ch = channel<Event>();
    auto subscription = std::make_shared<typename Channel<Event>::Subscription>();
    subscription->callback = std::move(callback);

    {
        std::lock_guard<std::mutex> lock(ch.mutex);
        ch.subscribers.push_back(subscription);
    }

    subscriber_count_.fetch_add(1, std::memory_order_relaxed);

    auto* channel_ptr = &ch;
    return EventSubscriber([this, channel_ptr, subscription]() {
        bool removed = false;
        {
            std::lock_guard<std::mutex> lock(channel_ptr->mutex);
            auto& subs = channel_ptr->subscribers;
            auto it = std::remove(subs.begin(), subs.end(), subscription);
            if (it != subs.end()) {
                subs.erase(it, subs.end());
                removed = true;
            }
        }

        if (removed) {
            subscriber_count_.fetch_sub(1, std::memory_order_relaxed);
        }
    });
}

template <typename Event>
bool EventBusV2::getLatest(Event& out) const {
    auto& ch = channel<Event>();
    std::lock_guard<std::mutex> lock(ch.mutex);
    if (!ch.latest.has_value()) {
        return false;
    }
    out = *ch.latest;
    return true;
}

template <typename Event>
bool EventBusV2::hasLatest() const {
    auto& ch = channel<Event>();
    std::lock_guard<std::mutex> lock(ch.mutex);
    return ch.latest.has_value();
}

template <typename Event>
typename EventBusV2::Channel<Event>& EventBusV2::channel() const {
    static Channel<Event> instance;
    return instance;
}

template <typename Event>
void EventBusV2::fillMetadata(Event& event, std::true_type) {
    event.metadata.timestamp_ms = millis();
    event.metadata.sequence = sequence_counter_.fetch_add(1, std::memory_order_relaxed);
}

inline void EventBusV2::resetStats() {
    total_published_.store(0, std::memory_order_relaxed);
    total_delivered_.store(0, std::memory_order_relaxed);
}

inline BusStatistics EventBusV2::statistics() const {
    BusStatistics stats{};
    stats.total_published = total_published_.load(std::memory_order_relaxed);
    stats.total_delivered = total_delivered_.load(std::memory_order_relaxed);
    stats.subscriber_count = subscriber_count_.load(std::memory_order_relaxed);
    return stats;
}

inline void EventBusV2::fillMetadata(Event&, std::false_type) {}

} // namespace tinybms::event

