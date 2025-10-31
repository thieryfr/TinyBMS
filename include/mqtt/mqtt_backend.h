#pragma once

#include <Arduino.h>
#include <functional>

#include "mqtt/publisher.h"

namespace mqtt {

class MqttBackend {
public:
    enum class Event {
        Connected,
        Disconnected,
        Error
    };

    using EventCallback = std::function<void(Event, int32_t)>;

    virtual ~MqttBackend() = default;

    virtual bool start(const BrokerSettings& settings, EventCallback callback) = 0;
    virtual void stop() = 0;
    virtual bool publish(const char* topic,
                         const char* payload,
                         size_t length,
                         uint8_t qos,
                         bool retain) = 0;
    virtual bool isConnected() const = 0;
    virtual void loop() = 0;
};

} // namespace mqtt

