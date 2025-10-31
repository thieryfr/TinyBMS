#include "mqtt/victron_mqtt_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "logger.h"
#include "tiny_read_mapping.h"
#include "victron_state_utils.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <esp_event.h>
#include <mqtt_client.h>
#include <esp_timer.h>
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
#if defined(ARDUINO)
    return millis();
#elif defined(ESP_PLATFORM)
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
#else
    return 0;
#endif
}

uint8_t clampQos(uint8_t qos) {
    return (qos > 2) ? 2 : qos;
}

} // namespace

using tinybms::events::MqttRegisterValue;
using tinybms::events::MqttRegisterEvent;
using tinybms::events::AlarmRaised;
using tinybms::events::AlarmCleared;
using tinybms::events::WarningRaised;

#define MQTT_LOG(level, msg) do { logger.log(level, String("[MQTT] ") + (msg)); } while(0)

VictronMqttBridge::VictronMqttBridge(tinybms::event::EventBusV2& bus)
    : bus_(bus)
    , bus_subscription_()
    , alarm_subscription_()
    , alarm_cleared_subscription_()
    , warning_subscription_()
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
    , last_voltage_(0.0f)
    , last_current_(0.0f)
    , last_voltage_timestamp_ms_(0)
    , last_current_timestamp_ms_(0)
    , voltage_valid_(false)
    , current_valid_(false)
    , announced_derivatives_(false)
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    , client_(nullptr)
#endif
{
}

VictronMqttBridge::~VictronMqttBridge() {
    disconnect();
    bus_subscription_.unsubscribe();
    alarm_subscription_.unsubscribe();
    alarm_cleared_subscription_.unsubscribe();
    warning_subscription_.unsubscribe();
}

void VictronMqttBridge::enable(bool enabled) {
    enabled_ = enabled;
    if (!enabled_) {
        disconnect();
    }
}

bool VictronMqttBridge::begin() {
    if (bus_subscription_.isActive()) {
        return true;
    }

    bus_subscription_ = bus_.subscribe<MqttRegisterValue>(
        [this](const MqttRegisterValue& event) {
            handleRegisterEvent(event);
        });

    if (!bus_subscription_.isActive()) {
        noteError(1, "Event bus subscription failed");
        MQTT_LOG(LOG_ERROR, "Failed to subscribe to MQTT register events");
        return false;
    }

    alarm_subscription_ = bus_.subscribe<AlarmRaised>(
        [this](const AlarmRaised& event) {
            handleAlarmEvent(event);
        });

    alarm_cleared_subscription_ = bus_.subscribe<AlarmCleared>(
        [this](const AlarmCleared& event) {
            handleAlarmCleared(event);
        });

    warning_subscription_ = bus_.subscribe<WarningRaised>(
        [this](const WarningRaised& event) {
            handleWarningEvent(event);
        });

    if (!alarm_subscription_.isActive() || !alarm_cleared_subscription_.isActive() || !warning_subscription_.isActive()) {
        noteError(12, "Alarm subscription failed");
        MQTT_LOG(LOG_WARN, "Failed to subscribe to Victron alarm events");
    }

    MQTT_LOG(LOG_INFO, "Subscribed to MQTT register events");
    return true;
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

#if defined(ARDUINO) || defined(ESP_PLATFORM)
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }

    connected_ = false;

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
    MQTT_LOG(LOG_INFO, "MQTT client simulated as connected (unsupported platform)");
    return true;
#endif
}

void VictronMqttBridge::disconnect() {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    if (client_) {
        esp_mqtt_client_stop(client_);
        esp_mqtt_client_destroy(client_);
        client_ = nullptr;
    }
#endif
    connecting_ = false;
    connected_ = false;
    voltage_valid_ = false;
    current_valid_ = false;
    last_connect_attempt_ms_ = 0;
}

void VictronMqttBridge::loop() {
    if (!enabled_ || !configured_) {
        return;
    }
#if defined(ARDUINO) || defined(ESP_PLATFORM)
    const uint32_t now = currentMillis();
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
    if (!value.dbus_path.isEmpty()) {
        doc["dbus_path"] = value.dbus_path;
    }

    String payload;
    serializeJson(doc, payload);

    const uint8_t qos = (qos_override != 255)
        ? clampQos(qos_override)
        : clampQos(settings_.default_qos);
    const bool retain = retain_override || settings_.retain_by_default;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
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
        static_cast<int>(payload.length()),
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
    return connected_;
}

void VictronMqttBridge::appendStatus(JsonObject obj) const {
    obj["enabled"] = enabled_;
    obj["configured"] = configured_;
    obj["subscribed"] = bus_subscription_.isActive();
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

void VictronMqttBridge::announceDerivedTopics() {
    if (!announced_derivatives_) {
        MQTT_LOG(LOG_DEBUG, "Derived Victron topics active (legacy schema preserved)");
        announced_derivatives_ = true;
    }
}

void VictronMqttBridge::publishDerived(RegisterValue value) {
    if (value.timestamp_ms == 0) {
        value.timestamp_ms = currentMillis();
    }
    announceDerivedTopics();
    publishRegister(value);
}

String VictronMqttBridge::alarmSuffixFromPath(const char* path) const {
    if (!path || path[0] == '\0') {
        return String();
    }
    if (std::strcmp(path, "/Alarms/LowVoltage") == 0) {
        return String("alarm_low_voltage");
    }
    if (std::strcmp(path, "/Alarms/HighVoltage") == 0) {
        return String("alarm_high_voltage");
    }
    if (std::strcmp(path, "/Alarms/HighTemperature") == 0) {
        return String("alarm_overtemperature");
    }
    if (std::strcmp(path, "/Alarms/CellImbalance") == 0) {
        return String("alarm_cell_imbalance");
    }
    if (std::strcmp(path, "/Alarms/Communication") == 0) {
        return String("alarm_communication");
    }
    if (std::strcmp(path, "/Alarms/SystemShutdown") == 0) {
        return String("alarm_system_shutdown");
    }
    if (std::strcmp(path, "/Alarms/LowTemperatureCharge") == 0) {
        return String("alarm_low_temperature_charge");
    }
    return String();
}

void VictronMqttBridge::publishSystemState(uint16_t tiny_status, uint32_t timestamp_ms) {
    victron::SystemStateInfo info = victron::mapOnlineStatus(tiny_status);

    RegisterValue derived;
    derived.address = 50;
    derived.key = "system_state";
    derived.label = "Victron System State";
    derived.unit = "-";
    derived.value_class = TinyRegisterValueClass::Enum;
    derived.wire_type = TinyRegisterValueType::Uint16;
    derived.has_numeric_value = true;
    derived.numeric_value = static_cast<float>(info.code);
    derived.raw_value = static_cast<int32_t>(tiny_status);
    derived.raw_word_count = 0;
    derived.precision = 0;
    derived.scale = 1.0f;
    derived.offset = 0.0f;
    derived.default_value = 0.0f;
    derived.timestamp_ms = timestamp_ms;
    derived.topic_suffix = "system_state";
    derived.dbus_path = "/System/0/State";
    derived.has_text_value = true;
    derived.text_value = info.label;
    derived.comment = String("TinyBMS status 0x") + String(tiny_status, HEX) + " mapped to Victron state";

    publishDerived(derived);
}

void VictronMqttBridge::publishVictronAlarm(const tinybms::events::AlarmEvent& alarm,
                                            uint32_t timestamp_ms,
                                            bool active) {
    if (alarm.victron_bit == 255 || alarm.victron_path[0] == '\0') {
        return;
    }

    String suffix = alarmSuffixFromPath(alarm.victron_path);
    if (suffix.length() == 0) {
        return;
    }

    RegisterValue derived;
    derived.address = alarm.alarm_code;
    derived.key = suffix;
    derived.label = String("Victron ") + suffix;
    derived.unit = "-";
    derived.value_class = TinyRegisterValueClass::Enum;
    derived.wire_type = TinyRegisterValueType::Uint16;
    derived.has_numeric_value = true;
    const uint8_t level = active ? alarm.victron_level : 0u;
    derived.numeric_value = static_cast<float>(level);
    derived.raw_value = static_cast<int32_t>(level);
    derived.raw_word_count = 0;
    derived.precision = 0;
    derived.scale = 1.0f;
    derived.offset = 0.0f;
    derived.default_value = 0.0f;
    derived.timestamp_ms = timestamp_ms;
    derived.topic_suffix = suffix;
    derived.dbus_path = alarm.victron_path;
    derived.has_text_value = true;
    derived.text_value = active ? String(alarm.message) : String("cleared");
    derived.comment = String("Victron alarm bit ") + String(alarm.victron_bit) + (active ? " active" : " cleared");

    publishDerived(derived);
}

void VictronMqttBridge::processDerivedRegister(const RegisterValue& value) {
    const String& suffix = value.topic_suffix;

    if (suffix.equalsIgnoreCase("battery_pack_voltage") && value.has_numeric_value) {
        last_voltage_ = value.numeric_value;
        last_voltage_timestamp_ms_ = value.timestamp_ms;
        voltage_valid_ = true;
    } else if (suffix.equalsIgnoreCase("battery_pack_current") && value.has_numeric_value) {
        last_current_ = value.numeric_value;
        last_current_timestamp_ms_ = value.timestamp_ms;
        current_valid_ = true;
    } else if (suffix.equalsIgnoreCase("system_status") && value.has_numeric_value) {
        publishSystemState(static_cast<uint16_t>(value.raw_value & 0xFFFF), value.timestamp_ms);
    }

    if (voltage_valid_ && current_valid_) {
        const uint32_t latest_ts = (value.timestamp_ms != 0) ? value.timestamp_ms : currentMillis();
        RegisterValue derived;
        derived.address = 0;
        derived.key = "pack_power_w";
        derived.label = "Pack Power";
        derived.unit = "W";
        derived.value_class = TinyRegisterValueClass::Float;
        derived.wire_type = TinyRegisterValueType::Float;
        derived.has_numeric_value = true;
        const float power = last_voltage_ * last_current_;
        derived.numeric_value = power;
        derived.raw_value = static_cast<int32_t>(power);
        derived.raw_word_count = 0;
        derived.scale = 1.0f;
        derived.offset = 0.0f;
        derived.precision = 1;
        derived.default_value = 0.0f;
        derived.timestamp_ms = latest_ts;
        derived.topic_suffix = "pack_power_w";
        derived.dbus_path = "/Dc/0/Power";
        derived.has_text_value = false;
        derived.comment = "Derived from voltage and current";

        publishDerived(derived);
    }
}

void VictronMqttBridge::handleAlarmEvent(const AlarmRaised& event) {
    publishVictronAlarm(event.alarm, event.metadata.timestamp_ms, true);
}

void VictronMqttBridge::handleAlarmCleared(const AlarmCleared& event) {
    publishVictronAlarm(event.alarm, event.metadata.timestamp_ms, false);
}

void VictronMqttBridge::handleWarningEvent(const WarningRaised& event) {
    publishVictronAlarm(event.alarm, event.metadata.timestamp_ms, true);
}

void VictronMqttBridge::handleRegisterEvent(const MqttRegisterValue& event) {
    if (!enabled_ || !configured_) {
        return;
    }

    const MqttRegisterEvent& payload = event.payload;
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
    processDerivedRegister(value);
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)
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
            self->last_connect_attempt_ms_ = currentMillis();
            self->noteError(static_cast<uint32_t>(event_id), "Disconnected");
            MQTT_LOG(LOG_WARN, "MQTT disconnected");
            break;
        case MQTT_EVENT_ERROR:
            self->connected_ = false;
            self->connecting_ = false;
            self->failed_publish_count_++;
            self->last_connect_attempt_ms_ = currentMillis();
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

