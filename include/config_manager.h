#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARNING = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3
};

extern SemaphoreHandle_t configMutex;

class ConfigManager {
public:
    ConfigManager();

    bool begin(const char* filename = "/config.json");
    bool save();

    struct WiFiConfig {
        String ssid = "YourSSID";
        String password = "YourPassword";
        struct APFallback {
            bool enabled = true;
            String ssid = "TinyBMS-Bridge";
            String password = "12345678";
        } ap_fallback;
        String hostname = "tinybms-bridge";
    } wifi;

    struct HardwareConfig {
        struct UART {
            int rx_pin = 16;
            int tx_pin = 17;
            int baudrate = 115200;
            int timeout_ms = 1000;
        } uart;
        struct CAN {
            int tx_pin = 5;
            int rx_pin = 4;
            uint32_t bitrate = 250000;
            String mode = "normal";
        } can;
    } hardware;

    struct TinyBMSConfig {
        uint32_t poll_interval_ms = 100;
        uint8_t uart_retry_count = 3;
        uint32_t uart_retry_delay_ms = 50;
        bool broadcast_expected = true;
    } tinybms;

    struct VictronConfig {
        uint32_t pgn_update_interval_ms = 1000;
        uint32_t cvl_update_interval_ms = 20000;
        uint32_t keepalive_interval_ms = 1000;
        uint32_t keepalive_timeout_ms = 10000;
        String manufacturer_name = "TinyBMS";
        String battery_name = "Lithium Battery";
        struct Thresholds {
            float undervoltage_v = 44.0f;
            float overvoltage_v = 58.4f;
            float overtemp_c = 55.0f;
            float low_temp_charge_c = 0.0f;
            uint16_t imbalance_warn_mv = 100;
            uint16_t imbalance_alarm_mv = 200;
            float soc_low_percent = 10.0f;
            float soc_high_percent = 99.0f;
            float derate_current_a = 1.0f;
        } thresholds;
    } victron;

    struct CVLConfig {
        bool enabled = true;
        float bulk_soc_threshold = 90.0f;
        float transition_soc_threshold = 95.0f;
        float float_soc_threshold = 98.0f;
        float float_exit_soc = 95.0f;
        float float_approach_offset_mv = 50.0f;
        float float_offset_mv = 100.0f;
        float minimum_ccl_in_float_a = 5.0f;
        uint16_t imbalance_hold_threshold_mv = 100;
        uint16_t imbalance_release_threshold_mv = 50;
    } cvl;

    struct WebServerConfig {
        uint16_t port = 80;
        uint32_t websocket_update_interval_ms = 1000;
        bool enable_cors = true;
        bool enable_auth = false;
        String username = "admin";
        String password = "admin";
    } web_server;

    struct LoggingConfig {
        uint32_t serial_baudrate = 115200;
        LogLevel log_level = LOG_INFO;
        bool log_uart_traffic = false;
        bool log_can_traffic = false;
        bool log_cvl_changes = true;
    } logging;

    struct AdvancedConfig {
        bool enable_spiffs = true;
        bool enable_ota = true;
        uint32_t watchdog_timeout_s = 5;
        uint32_t stack_size_bytes = 8192;
    } advanced;

    bool isLoaded() const { return loaded_; }

private:
    void loadWiFiConfig(const JsonDocument& doc);
    void loadHardwareConfig(const JsonDocument& doc);
    void loadTinyBMSConfig(const JsonDocument& doc);
    void loadVictronConfig(const JsonDocument& doc);
    void loadCVLConfig(const JsonDocument& doc);
    void loadWebServerConfig(const JsonDocument& doc);
    void loadLoggingConfig(const JsonDocument& doc);
    void loadAdvancedConfig(const JsonDocument& doc);

    void saveWiFiConfig(JsonDocument& doc) const;
    void saveHardwareConfig(JsonDocument& doc) const;
    void saveTinyBMSConfig(JsonDocument& doc) const;
    void saveVictronConfig(JsonDocument& doc) const;
    void saveCVLConfig(JsonDocument& doc) const;
    void saveWebServerConfig(JsonDocument& doc) const;
    void saveLoggingConfig(JsonDocument& doc) const;
    void saveAdvancedConfig(JsonDocument& doc) const;

    LogLevel parseLogLevel(const String& level) const;
    const char* logLevelToString(LogLevel level) const;
    void printConfig() const;

private:
    String filename_;
    bool loaded_;
};
