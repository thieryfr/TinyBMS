#include "event/event_subscriber.h"

#include <utility>

namespace tinybms::event {

EventSubscriber::EventSubscriber(std::function<void()> unsubscribe)
    : unsubscribe_(std::move(unsubscribe)) {}

EventSubscriber::EventSubscriber(EventSubscriber&& other) noexcept
    : unsubscribe_(std::move(other.unsubscribe_)) {}

EventSubscriber& EventSubscriber::operator=(EventSubscriber&& other) noexcept {
    if (this != &other) {
        unsubscribe_ = std::move(other.unsubscribe_);
    }
    return *this;
}

EventSubscriber::~EventSubscriber() {
    unsubscribe();
}

void EventSubscriber::unsubscribe() {
    if (unsubscribe_) {
        auto unsubscribe = std::move(unsubscribe_);
        unsubscribe_ = nullptr;
        unsubscribe();
    }
}

} // namespace tinybms::event
