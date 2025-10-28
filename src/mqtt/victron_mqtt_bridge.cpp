#include "mqtt/victron_mqtt_bridge.h"

#include <algorithm>
#include <cctype>

#include "logger.h"
#include "tiny_read_mapping.h"

#ifdef ARDUINO
#include <esp_event.h>
#include <mqtt_client.h>
#endif

extern Logger logger;

namespace mqtt {
namespace {

String sanitizeSegment(const String& candidate) {
    if (candidate.length() == 0) {
        return String();
    }

    String sanitized;
    sanitized.reserve(candidate.length());

    for (size_t i = 0; i < candidate.length(); ++i) {
        char c = candidate.charAt(i);
        if (std::isalnum(static_cast<unsigned char>(c))) {
            sanitized += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (c == ' ' || c == '-' || c == '_' || c == '.') {
            if (sanitized.length() > 0 && sanitized.charAt(sanitized.length() - 1) == '_') {
                continue;
            }
            sanitized += '_';
        }
    }

    while (sanitized.length() > 0 && sanitized.charAt(sanitized.length() - 1) == '_') {
        sanitized.remove(sanitized.length() - 1);
    }

    return sanitized;
}

String sanitizeRootTopic(const String& raw) {
    String trimmed = raw;
    trimmed.trim();
    if (trimmed.length() == 0) {
        return String();
    }

    String result;
    int start = 0;
    while (start < trimmed.length()) {
        int slash = trimmed.indexOf('/', start);
        if (slash < 0) {
            slash = trimmed.length();
        }
        String segment = trimmed.substring(start, slash);
        segment.trim();
        String sanitized = sanitizeSegment(segment);
        if (sanitized.length() > 0) {
            if (result.length() > 0) {
                result += '/';
            }
            result += sanitized;
        }
        start = slash + 1;
    }

    return result;
}

inline uint32_t currentMillis() {
#ifdef ARDUINO
    return millis();
#else
    return 0;
#endif
}

uint8_t clampQos(uint8_t qos) {
    return (qos > 2) ? 2 : qos;
}

} // namespace

#define MQTT_LOG(level, msg) do { logger.log(level, String("[MQTT] ") + (msg)); } while(0)

VictronMqttBridge::VictronMqttBridge(EventBus& bus)
    : bus_(bus)
    , subscribed_(false)
    , enabled_(false)
    , configured_(false)
    , connecting_(false)
    , connected_(false)
    , sanitized_root_topic_("")
    , publish_count_(0)
    , failed_publish_count_(0)
    , last_publish_timestamp_ms_(0)
    , last_connect_attempt_ms_(0)
    , last_error_code_(0)
#ifdef ARDUINO
    , client_(nullptr)
#endif
{
}

VictronMqttBridge::~VictronMqttBridge() {
    disconnect();
    if (subscribed_) {
        bus_.unsubscribe(EVENT_MQTT_REGISTER_VALUE, &VictronMqttBridge::onBusEvent);
        subscribed_ = false;
    }
}

void VictronMqttBridge::enable(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        disconnect();
    }
}

bool VictronMqttBridge::begin() {
    if (subscribed_) {
        return true;
    }

    subscribed_ = bus_.subscribe(EVENT_MQTT_REGISTER_VALUE, &VictronMqttBridge::onBusEvent, this);
    if (!subscribed_) {
        noteError(1, "Event bus subscription failed");
        MQTT_LOG(LOG_ERROR, "Failed to subscribe to EVENT_MQTT_REGISTER_VALUE");
    } else {
        MQTT_LOG(LOG_INFO, "Subscribed to EVENT_MQTT_REGISTER_VALUE");
    }
    return subscribed_;
}

void VictronMqttBridge::configure(const BrokerSettings& settings) {
    settings_ = settings;
    settings_.default_qos = clampQos(settings.default_qos);
    sanitized_root_topic_ = sanitizeRootTopic(settings.root_topic);
    configured_ = true;
    MQTT_LOG(LOG_INFO, String("Configured MQTT broker: ") + settings_.uri + ":" + settings_.port);
}

bool VictronMqttBridge::shouldAttemptReconnect(uint32_t now_ms) const {
    if (!enabled_ || !configured_) {
        return false;
    }
    if (connecting_) {
        return false;
    }
    if (last_connect_attempt_ms_ == 0) {
        return true;
    }
    uint32_t interval = settings_.reconnect_interval_ms;
    if (interval == 0) {
        return true;
    }
    if (now_ms < last_connect_attempt_ms_) {
        return true;
    }
    return (now_ms - last_connect_attempt_ms_) >= interval;
}

void VictronMqttBridge::noteError(uint32_t code, const char* message) {
    last_error_code_ = code;
    if (message) {
        last_error_message_ = message;
    } else {
        last_error_message_.clear();
    }
}

bool VictronMqttBridge::connect() {
    if (!enabled_) {
        MQTT_LOG(LOG_DEBUG, "MQTT bridge disabled, skipping connect");
        return false;
    }
    if (!configured_) {
        noteError(2, "MQTT not configured");
        MQTT_LOG(LOG_WARN, "Cannot connect: configuration missing");
        return false;
    }

    last_connect_attempt_ms_ = currentMillis();

#ifdef ARDUINO
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = settings_.uri.c_str();
    cfg.broker.address.port = settings_.port;
    cfg.session.keepalive = settings_.keepalive_seconds;
    cfg.session.disable_clean_session = settings_.clean_session ? 0 : 1;
    cfg.credentials.client_id = settings_.client_id.c_str();
    if (settings_.username.length() > 0) {
        cfg.credentials.username = settings_.username.c_str();
    }
    if (settings_.password.length() > 0) {
        cfg.credentials.authentication.password = settings_.password.c_str();
    }
    cfg.network.disable_auto_reconnect = false;
    if (settings_.use_tls) {
        cfg.broker.verification.certificate = settings_.server_certificate.c_str();
    }

    client_ = esp_mqtt_client_init(&cfg);
    if (!client_) {
        noteError(3, "esp_mqtt_client_init failed");
        MQTT_LOG(LOG_ERROR, "Failed to init MQTT client");
        return false;
    }

    esp_err_t err = esp_mqtt_client_register_event(client_, MQTT_EVENT_ANY, &VictronMqttBridge::onMqttEvent, this);
    if (err != ESP_OK) {
        noteError(static_cast<uint32_t>(err), "register_event failed");
        MQTT_LOG(LOG_ERROR, String("Failed to register MQTT events: ") + static_cast<int>(err));
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return false;
    }

    connecting_ = true;
    err = esp_mqtt_client_start(client_);
    if (err != ESP_OK) {
        noteError(static_cast<uint32_t>(err), "client_start failed");
        connecting_ = false;
        MQTT_LOG(LOG_ERROR, String("Failed to start MQTT client: ") + static_cast<int>(err));
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
        return false;
    }

    MQTT_LOG(LOG_INFO, "MQTT client start requested");
    return true;
#else
    connecting_ = false;
    connected_ = true;
    noteError(0, nullptr);
    MQTT_LOG(LOG_INFO, "MQTT client simulated as connected (non-Arduino build)");
    return true;
#endif
}

void VictronMqttBridge::disconnect() {
#ifdef ARDUINO
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
#endif
    connecting_ = false;
    connected_ = false;
}

void VictronMqttBridge::loop() {
    if (!enabled_ || !configured_) {
        return;
    }
#ifdef ARDUINO
    const uint32_t now = millis();
    if (!connected_ && shouldAttemptReconnect(now)) {
        MQTT_LOG(LOG_WARN, "Attempting MQTT reconnect");
        connect();
    }
#else
    (void)shouldAttemptReconnect(currentMillis());
#endif
}

bool VictronMqttBridge::publishRegister(const RegisterValue& value,
                                        uint8_t qos_override,
                                        bool retain_override) {
    if (!enabled_ || !configured_) {
        return false;
    }

    const String topic = buildTopic(value.topic_suffix);
    if (topic.length() == 0) {
        failed_publish_count_++;
        noteError(10, "Empty topic");
        MQTT_LOG(LOG_WARN, "Dropping MQTT publish with empty topic");
        return false;
    }

    StaticJsonDocument<384> doc;
    doc["address"] = value.address;
    doc["timestamp_ms"] = value.timestamp_ms;
    doc["wire_type"] = static_cast<uint8_t>(value.wire_type);
    doc["value_class"] = static_cast<uint8_t>(value.value_class);
    if (value.has_numeric_value) {
        doc["value"] = value.numeric_value;
        if (value.precision > 0) {
            doc["formatted"] = String(value.numeric_value, value.precision);
        }
        doc["raw"] = value.raw_value;
        doc["scale"] = value.scale;
    }
    if (value.has_text_value) {
        doc["text"] = value.text_value;
    }
    if (!value.unit.isEmpty()) {
        doc["unit"] = value.unit;
    }
    if (!value.label.isEmpty()) {
        doc["label"] = value.label;
    }
    if (!value.key.isEmpty()) {
        doc["key"] = value.key;
    }
    if (!value.comment.isEmpty()) {
        doc["comment"] = value.comment;
    }

    String payload;
    serializeJson(doc, payload);

    const uint8_t qos = (qos_override != 255)
        ? clampQos(qos_override)
        : clampQos(settings_.default_qos);
    const bool retain = retain_override || settings_.retain_by_default;

#ifdef ARDUINO
    if (!client_) {
        failed_publish_count_++;
        noteError(11, "Client not initialised");
        MQTT_LOG(LOG_ERROR, "MQTT client not ready for publish");
        return false;
    }

    const int msg_id = esp_mqtt_client_publish(
        client_,
        topic.c_str(),
        payload.c_str(),
        payload.length(),
        qos,
        retain ? 1 : 0
    );

    if (msg_id < 0) {
        failed_publish_count_++;
        noteError(static_cast<uint32_t>(msg_id), "publish failed");
        MQTT_LOG(LOG_WARN, String("MQTT publish failed on topic ") + topic);
        return false;
    }
#else
    (void)qos;
    (void)retain;
#endif

    publish_count_++;
    last_publish_timestamp_ms_ = value.timestamp_ms;
    noteError(0, nullptr);
    return true;
}

bool VictronMqttBridge::isConnected() const {
#ifdef ARDUINO
    return connected_;
#else
    return connected_;
#endif
}

void VictronMqttBridge::appendStatus(JsonObject obj) const {
    obj["enabled"] = enabled_;
    obj["configured"] = configured_;
    obj["subscribed"] = subscribed_;
    obj["connected"] = connected_;
    obj["client_id"] = settings_.client_id;
    obj["root_topic"] = sanitized_root_topic_;
    obj["publish_count"] = publish_count_;
    obj["failed_count"] = failed_publish_count_;
    obj["last_publish_ms"] = last_publish_timestamp_ms_;
    obj["last_error_code"] = last_error_code_;
    obj["qos"] = clampQos(settings_.default_qos);
    obj["retain"] = settings_.retain_by_default;
    obj["clean_session"] = settings_.clean_session;
    obj["use_tls"] = settings_.use_tls;
    if (!last_error_message_.isEmpty()) {
        obj["last_error_message"] = last_error_message_;
    }
}

String VictronMqttBridge::buildTopic(const String& suffix) const {
    if (sanitized_root_topic_.length() == 0) {
        return suffix;
    }
    if (suffix.length() == 0) {
        return sanitized_root_topic_;
    }

    String topic = sanitized_root_topic_;
    if (!topic.endsWith("/")) {
        topic += '/';
    }
    topic += suffix;
    return topic;
}

void VictronMqttBridge::handleRegisterEvent(const MqttRegisterEvent& payload) {
    if (!enabled_ || !configured_) {
        return;
    }

    const TinyRegisterRuntimeBinding* binding = findTinyRegisterBinding(payload.address);
    if (!binding) {
        return;
    }

    float scaled_value = static_cast<float>(payload.raw_value) * binding->scale;
    String text_value;
    const String* text_ptr = nullptr;
    if (payload.has_text) {
        text_value = String(payload.text_value);
        text_ptr = &text_value;
    }

    RegisterValue value;
    if (!buildRegisterValue(*binding,
                             payload.raw_value,
                             scaled_value,
                             text_ptr,
                             payload.raw_words,
                             payload.timestamp_ms,
                             value)) {
        return;
    }

    publishRegister(value);
}

void VictronMqttBridge::onBusEvent(const BusEvent& event, void* user_data) {
    if (!user_data || event.type != EVENT_MQTT_REGISTER_VALUE) {
        return;
    }

    auto* self = static_cast<VictronMqttBridge*>(user_data);
    self->handleRegisterEvent(event.data.mqtt_register);
}

#ifdef ARDUINO
void VictronMqttBridge::onMqttEvent(void* handler_args,
                                    esp_event_base_t,
                                    int32_t event_id,
                                    void* event_data) {
    auto* self = static_cast<VictronMqttBridge*>(handler_args);
    if (!self) {
        return;
    }

    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            self->connecting_ = false;
            self->connected_ = true;
            self->noteError(0, nullptr);
            MQTT_LOG(LOG_INFO, "MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            self->connected_ = false;
            self->connecting_ = false;
            self->noteError(static_cast<uint32_t>(event_id), "Disconnected");
            MQTT_LOG(LOG_WARN, "MQTT disconnected");
            break;
        case MQTT_EVENT_ERROR:
            self->connected_ = false;
            self->connecting_ = false;
            self->failed_publish_count_++;
            self->noteError(static_cast<uint32_t>(event_id), "MQTT error event");
            MQTT_LOG(LOG_ERROR, "MQTT event error");
            break;
        default:
            (void)event_data;
            break;
    }
}
#endif

} // namespace mqtt

