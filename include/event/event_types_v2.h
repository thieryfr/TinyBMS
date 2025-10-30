#pragma once

#include <Arduino.h>
#include <cstdint>

#include "shared_data.h"

namespace tinybms::events {

enum class EventSource : uint32_t {
    Unknown = 0,
    Uart = 1,
    Can = 2,
    Websocket = 3,
    WebApi = 4,
    Cvl = 5,
    ConfigManager = 6,
    Watchdog = 7,
    Logger = 8,
    System = 9
};

enum class AlarmSeverity : uint8_t {
    Info = 0,
    Warning = 1,
    Error = 2,
    Critical = 3
};

enum class StatusLevel : uint8_t {
    Info = 0,
    Notice = 1,
    Warning = 2,
    Error = 3
};

enum class AlarmCode : uint16_t {
    None = 0,
    OverVoltage = 1,
    UnderVoltage = 2,
    CellOverVoltage = 3,
    CellUnderVoltage = 4,
    OverCurrentCharge = 10,
    OverCurrentDischarge = 11,
    OverTemperature = 20,
    UnderTemperature = 21,
    LowTempCharge = 22,
    CellImbalance = 30,
    UartError = 40,
    UartTimeout = 41,
    CanError = 42,
    CanTimeout = 43,
    CanTxError = 44,
    CanKeepAliveLost = 45,
    WatchdogReset = 50,
    ConfigError = 51,
    MemoryError = 52,
    BmsOffline = 60,
    BmsFault = 61
};

struct CVL_StateChange {
    uint8_t old_state = 0;
    uint8_t new_state = 0;
    float new_cvl_voltage = 0.0f;
    float new_ccl_current = 0.0f;
    float new_dcl_current = 0.0f;
    uint32_t state_duration_ms = 0;
};

struct AlarmEvent {
    uint16_t alarm_code = 0;
    uint8_t severity = static_cast<uint8_t>(AlarmSeverity::Error);
    char message[64] = {};
    float value = 0.0f;
    bool is_active = false;
};

struct ConfigChangeEvent {
    char config_path[64] = {};
    char old_value[32] = {};
    char new_value[32] = {};
};

struct CommandEvent {
    uint8_t command_type = 0;
    uint8_t parameters[32] = {};
    uint8_t parameter_length = 0;
    bool expects_response = false;
    uint32_t correlation_id = 0;
    bool success = false;
    char error_message[32] = {};
};

struct SystemStatusEvent {
    uint32_t uptime_ms = 0;
    uint32_t free_heap_bytes = 0;
    uint8_t cpu_usage_percent = 0;
    uint8_t wifi_rssi_dbm = 0;
    bool watchdog_enabled = false;
    uint32_t total_events_published = 0;
};

struct StatusEvent {
    char message[64] = {};
    uint8_t level = static_cast<uint8_t>(StatusLevel::Info);
};

struct WiFiEvent {
    char ssid[32] = {};
    int8_t rssi_dbm = 0;
    uint8_t ip_address[4] = {};
    bool is_connected = false;
};

struct WebSocketClientEvent {
    uint32_t client_id = 0;
    uint8_t ip_address[4] = {};
    bool is_connected = false;
};

struct MqttRegisterEvent {
    uint16_t address = 0;
    TinyRegisterValueType value_type = TinyRegisterValueType::Uint16;
    uint8_t raw_word_count = 0;
    int32_t raw_value = 0;
    bool has_text = false;
    char text_value[64] = {};
    uint16_t raw_words[TINY_REGISTER_MAX_WORDS] = {};
    uint32_t timestamp_ms = 0;
};

struct EventMetadata {
    uint32_t timestamp_ms = 0;
    uint32_t sequence = 0;
    EventSource source = EventSource::Unknown;
};

struct LiveDataUpdate {
    EventMetadata metadata{};
    TinyBMS_LiveData data{};
};

struct MqttRegisterValue {
    EventMetadata metadata{};
    MqttRegisterEvent payload{};
};

struct AlarmRaised {
    EventMetadata metadata{};
    AlarmEvent alarm{};
};

struct AlarmCleared {
    EventMetadata metadata{};
    AlarmEvent alarm{};
};

struct WarningRaised {
    EventMetadata metadata{};
    AlarmEvent alarm{};
};

struct ConfigChanged {
    EventMetadata metadata{};
    ConfigChangeEvent change{};
};

struct CVLStateChanged {
    EventMetadata metadata{};
    CVL_StateChange state{};
};

struct StatusMessage {
    EventMetadata metadata{};
    StatusLevel level = StatusLevel::Info;
    char message[64] = {};
};

} // namespace tinybms::events

