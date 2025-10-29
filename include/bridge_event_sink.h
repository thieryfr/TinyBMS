#pragma once

#include "event/event_bus_v2.h"

class BridgeEventSink {
public:
    virtual ~BridgeEventSink() = default;

    virtual bool isReady() const = 0;
    virtual void publish(const tinybms::events::LiveDataUpdate& event) = 0;
    virtual void publish(const tinybms::events::MqttRegisterValue& event) = 0;
    virtual void publish(const tinybms::events::AlarmRaised& event) = 0;
    virtual void publish(const tinybms::events::AlarmCleared& event) = 0;
    virtual void publish(const tinybms::events::WarningRaised& event) = 0;
    virtual void publish(const tinybms::events::StatusMessage& event) = 0;
    virtual void publish(const tinybms::events::CVLStateChanged& event) = 0;
    virtual bool latest(tinybms::events::LiveDataUpdate& event_out) const = 0;
};

BridgeEventSink& defaultBridgeEventSink(tinybms::event::EventBusV2& bus);
