#include "bridge_event_sink.h"

using tinybms::event::EventBusV2;
using namespace tinybms::events;

namespace {
class EventBusBridgeEventSink final : public BridgeEventSink {
public:
    EventBusBridgeEventSink() : bus_(nullptr) {}

    void setBus(EventBusV2& bus) { bus_ = &bus; }

    bool isReady() const override {
        return bus_ != nullptr;
    }

    void publish(const LiveDataUpdate& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    void publish(const MqttRegisterValue& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    void publish(const AlarmRaised& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    void publish(const AlarmCleared& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    void publish(const WarningRaised& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    void publish(const StatusMessage& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    void publish(const CVLStateChanged& event) override {
        if (bus_) {
            bus_->publish(event);
        }
    }

    bool latest(LiveDataUpdate& event_out) const override {
        return bus_ != nullptr && bus_->getLatest(event_out);
    }

private:
    EventBusV2* bus_;
};
}  // namespace

BridgeEventSink& defaultBridgeEventSink(EventBusV2& bus) {
    static EventBusBridgeEventSink sink;
    sink.setBus(bus);
    return sink;
}
