#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "mqtt/publisher.h"
#include "event/event_bus_v2.h"
#include "event/event_types_v2.h"

namespace mqtt {

class VictronMqttBridge : public Publisher {
public:
    explicit VictronMqttBridge(tinybms::event::EventBusV2& bus);
    ~VictronMqttBridge() override;

    void enable(bool enabled);
    bool begin();

    void configure(const BrokerSettings& settings) override;
    bool connect() override;
    void disconnect() override;
    void loop() override;
    bool publishRegister(const RegisterValue& value,
                         uint8_t qos_override = 255,
                         bool retain_override = false) override;
    bool isConnected() const override;

    void appendStatus(JsonObject obj) const;

private:
#ifdef ARDUINO
    static void onMqttEvent(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
#endif
    void handleRegisterEvent(const tinybms::events::MqttRegisterValue& event);

    String buildTopic(const String& suffix) const;
    bool shouldAttemptReconnect(uint32_t now_ms) const;
    void noteError(uint32_t code, const char* message);

private:
    tinybms::event::EventBusV2& bus_;
    tinybms::event::EventSubscriber bus_subscription_;
    bool enabled_;
    bool configured_;
    bool connecting_;
    bool connected_;
    BrokerSettings settings_;
    String sanitized_root_topic_;
    uint32_t publish_count_;
    uint32_t failed_publish_count_;
    uint32_t last_publish_timestamp_ms_;
    uint32_t last_connect_attempt_ms_;
    uint32_t last_error_code_;
    String last_error_message_;
#ifdef ARDUINO
    esp_mqtt_client_handle_t client_;
#endif
};

} // namespace mqtt

