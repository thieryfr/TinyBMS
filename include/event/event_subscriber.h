#pragma once

#include <functional>

namespace tinybms::event {

class EventSubscriber {
public:
    EventSubscriber() = default;
    explicit EventSubscriber(std::function<void()> unsubscribe);
    EventSubscriber(EventSubscriber&& other) noexcept;
    EventSubscriber& operator=(EventSubscriber&& other) noexcept;
    ~EventSubscriber();

    EventSubscriber(const EventSubscriber&) = delete;
    EventSubscriber& operator=(const EventSubscriber&) = delete;

    void unsubscribe();
    bool isActive() const { return unsubscribe_ != nullptr; }

private:
    std::function<void()> unsubscribe_{};
};

} // namespace tinybms::event
