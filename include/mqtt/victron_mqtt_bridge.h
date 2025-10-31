#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_event.h>
#include <mqtt_client.h>
#endif

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
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    static void onMqttEvent(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
#endif
    void handleRegisterEvent(const tinybms::events::MqttRegisterValue& event);
    void handleAlarmEvent(const tinybms::events::AlarmRaised& event);
    void handleAlarmCleared(const tinybms::events::AlarmCleared& event);
    void handleWarningEvent(const tinybms::events::WarningRaised& event);
    void processDerivedRegister(const RegisterValue& value);
    void publishDerived(RegisterValue value);
    void publishSystemState(uint16_t tiny_status, uint32_t timestamp_ms);
    void publishVictronAlarm(const tinybms::events::AlarmEvent& alarm,
                             uint32_t timestamp_ms,
                             bool active);
    String alarmSuffixFromPath(const char* path) const;
    void announceDerivedTopics();

    String buildTopic(const String& suffix) const;
    bool shouldAttemptReconnect(uint32_t now_ms) const;
    void noteError(uint32_t code, const char* message);

private:
    tinybms::event::EventBusV2& bus_;
    tinybms::event::EventSubscriber bus_subscription_;
    tinybms::event::EventSubscriber alarm_subscription_;
    tinybms::event::EventSubscriber alarm_cleared_subscription_;
    tinybms::event::EventSubscriber warning_subscription_;
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
    float last_voltage_;
    float last_current_;
    uint32_t last_voltage_timestamp_ms_;
    uint32_t last_current_timestamp_ms_;
    bool voltage_valid_;
    bool current_valid_;
    bool announced_derivatives_;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    esp_mqtt_client_handle_t client_;
#endif
};

} // namespace mqtt

