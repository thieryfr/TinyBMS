#include "mqtt/esp_idf_mqtt_backend.h"

#if defined(ESP_PLATFORM)

#include <esp_log.h>
#include <utility>

namespace mqtt {

namespace {
constexpr const char* TAG = "EspIdfMqttBackend";
}

EspIdfMqttBackend::EspIdfMqttBackend()
    : client_(nullptr)
    , connected_(false)
    , callback_() {
}

EspIdfMqttBackend::~EspIdfMqttBackend() {
    stop();
}

bool EspIdfMqttBackend::start(const BrokerSettings& settings, EventCallback callback) {
    stop();

    callback_ = std::move(callback);
    connected_ = false;

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = settings.uri.c_str();
    cfg.broker.address.port = settings.port;
    cfg.session.keepalive = settings.keepalive_seconds;
    cfg.session.disable_clean_session = settings.clean_session ? 0 : 1;
    cfg.credentials.client_id = settings.client_id.c_str();
    if (settings.username.length() > 0) {
        cfg.credentials.username = settings.username.c_str();
    }
    if (settings.password.length() > 0) {
        cfg.credentials.authentication.password = settings.password.c_str();
    }
    cfg.network.disable_auto_reconnect = false;
    if (settings.use_tls && settings.server_certificate.length() > 0) {
        cfg.broker.verification.certificate = settings.server_certificate.c_str();
    }

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        callback_ = nullptr;
        return false;
    }

    esp_err_t err = esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &EspIdfMqttBackend::handleEvent, this);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register_event failed: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        callback_ = nullptr;
        return false;
    }

    err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "client_start failed: %s", esp_err_to_name(err));
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        callback_ = nullptr;
        return false;
    }

    return true;
}

void EspIdfMqttBackend::stop() {
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
    connected_ = false;
    callback_ = nullptr;
}

bool EspIdfMqttBackend::publish(const char* topic,
                                const char* payload,
                                size_t length,
                                uint8_t qos,
                                bool retain) {
    if (!client_) {
        return false;
    }

    const int msg_id = esp_mqtt_client_publish(
        client_,
        topic,
        payload,
        static_cast<int>(length),
        qos,
        retain ? 1 : 0);

    return msg_id >= 0;
}

bool EspIdfMqttBackend::isConnected() const {
    return connected_;
}

void EspIdfMqttBackend::loop() {
    // esp-mqtt runs on its own task; nothing required here.
}

void EspIdfMqttBackend::handleEvent(void* handler_args,
                                    esp_event_base_t,
                                    int32_t event_id,
                                    void*) {
    auto* self = static_cast<EspIdfMqttBackend*>(handler_args);
    if (!self) {
        return;
    }

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            self->connected_ = true;
            self->dispatch(Event::Connected, 0);
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->connected_ = false;
            self->dispatch(Event::Disconnected, event_id);
            break;
        case MQTT_EVENT_ERROR:
            self->connected_ = false;
            self->dispatch(Event::Error, event_id);
            break;
        default:
            break;
    }
}

void EspIdfMqttBackend::dispatch(Event event, int32_t data) {
    if (callback_) {
        callback_(event, data);
    }
}

} // namespace mqtt

#endif // defined(ESP_PLATFORM)

