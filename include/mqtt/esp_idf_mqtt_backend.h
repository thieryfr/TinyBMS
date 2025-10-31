#pragma once

#if defined(ESP_PLATFORM)

#include <esp_event.h>
#include <mqtt_client.h>

#include "mqtt/mqtt_backend.h"

namespace mqtt {

class EspIdfMqttBackend : public MqttBackend {
public:
    EspIdfMqttBackend();
    ~EspIdfMqttBackend() override;

    bool start(const BrokerSettings& settings, EventCallback callback) override;
    void stop() override;
    bool publish(const char* topic,
                 const char* payload,
                 size_t length,
                 uint8_t qos,
                 bool retain) override;
    bool isConnected() const override;
    void loop() override;

private:
    static void handleEvent(void* handler_args,
                            esp_event_base_t base,
                            int32_t event_id,
                            void* event_data);
    void dispatch(Event event, int32_t data);

    esp_mqtt_client_handle_t client_;
    bool connected_;
    EventCallback callback_;
};

} // namespace mqtt

#endif // defined(ESP_PLATFORM)

