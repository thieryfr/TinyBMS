/**
 * @file web_routes_api.cpp
 * @brief API routes for system management (with Logging)
 * @version 1.1 - Logging + Mutex Safety
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <Freertos.h>
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "logger.h"          // ✅ Added for logging
#include "config_manager.h"  // Needed for config references
#include "watchdog_manager.h"
#include "web_routes.h"
#include "event/event_bus_v2.h"
#include "hal/hal_manager.h"
#include "hal/interfaces/ihal_can.h"
#include "tinybms_victron_bridge.h"
#include "victron_can_mapping.h"

// External globals
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t feedMutex;
extern Logger logger;
using tinybms::event::eventBus;
extern TinyBMS_Victron_Bridge bridge;

// External functions
extern String getStatusJSON();
extern String getSystemConfigJSON();

namespace {

void sendJsonResponse(AsyncWebServerRequest* request, int statusCode, JsonDocument& doc) {
    String payload;
    serializeJson(doc, payload);
    request->send(statusCode, "application/json", payload);
}

void sendErrorResponse(AsyncWebServerRequest* request, int statusCode, const String& message, const char* code = nullptr) {
    StaticJsonDocument<256> doc;
    doc["success"] = false;
    doc["message"] = message;
    if (code != nullptr) {
        doc["error"] = code;
    }
    sendJsonResponse(request, statusCode, doc);
}

String logLevelToLowercase(LogLevel level) {
    switch (level) {
        case LOG_ERROR:   return "error";
        case LOG_WARNING: return "warning";
        case LOG_DEBUG:   return "debug";
        case LOG_INFO:
        default:          return "info";
    }
}

LogLevel parseLogLevelFromString(const String& levelStr) {
    String upper = levelStr;
    upper.toUpperCase();
    if (upper == "ERROR") return LOG_ERROR;
    if (upper == "WARNING" || upper == "WARN") return LOG_WARNING;
    if (upper == "DEBUG") return LOG_DEBUG;
    return LOG_INFO;
}

bool buildSettingsSnapshot(JsonObject configObj, String& errorMessage) {
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        errorMessage = "config_mutex_timeout";
        return false;
    }

    JsonObject wifi = configObj.createNestedObject("wifi");
    wifi["mode"] = config.wifi.mode;
    wifi["ssid"] = config.wifi.sta_ssid;
    wifi["sta_ssid"] = config.wifi.sta_ssid;
    wifi["password"] = config.wifi.sta_password;
    wifi["sta_password"] = config.wifi.sta_password;
    wifi["hostname"] = config.wifi.sta_hostname;
    wifi["sta_hostname"] = config.wifi.sta_hostname;
    wifi["sta_ip_mode"] = config.wifi.sta_ip_mode;
    wifi["sta_static_ip"] = config.wifi.sta_static_ip;
    wifi["sta_gateway"] = config.wifi.sta_gateway;
    wifi["sta_subnet"] = config.wifi.sta_subnet;
    wifi["ap_ssid"] = config.wifi.ap_fallback.ssid;
    wifi["ap_password"] = config.wifi.ap_fallback.password;
    wifi["ap_channel"] = config.wifi.ap_fallback.channel;
    wifi["ap_fallback"] = config.wifi.ap_fallback.enabled;

    JsonObject hw = configObj.createNestedObject("hardware");
    hw["uart_rx_pin"] = config.hardware.uart.rx_pin;
    hw["uart_tx_pin"] = config.hardware.uart.tx_pin;
    hw["uart_baudrate"] = config.hardware.uart.baudrate;
    hw["uart_timeout_ms"] = config.hardware.uart.timeout_ms;
    hw["can_tx_pin"] = config.hardware.can.tx_pin;
    hw["can_rx_pin"] = config.hardware.can.rx_pin;
    hw["can_bitrate"] = config.hardware.can.bitrate;
    hw["can_mode"] = config.hardware.can.mode;
    hw["can_termination"] = config.hardware.can.termination;

    JsonObject cvl = configObj.createNestedObject("cvl");
    cvl["enabled"] = config.cvl.enabled;
    cvl["bulk_transition_soc"] = config.cvl.bulk_soc_threshold;
    cvl["transition_float_soc"] = config.cvl.transition_soc_threshold;
    cvl["float_soc_threshold"] = config.cvl.float_soc_threshold;
    cvl["float_exit_soc"] = config.cvl.float_exit_soc;
    cvl["float_approach_offset"] = config.cvl.float_approach_offset_mv;
    cvl["float_offset"] = config.cvl.float_offset_mv;
    cvl["minimum_ccl_in_float_a"] = config.cvl.minimum_ccl_in_float_a;
    cvl["imbalance_trigger_mv"] = config.cvl.imbalance_hold_threshold_mv;
    cvl["imbalance_release_mv"] = config.cvl.imbalance_release_threshold_mv;
    cvl["imbalance_offset"] = config.cvl.minimum_ccl_in_float_a;

    JsonObject victron = configObj.createNestedObject("victron");
    victron["manufacturer"] = config.victron.manufacturer_name;
    victron["battery_name"] = config.victron.battery_name;
    victron["pgn_interval_ms"] = config.victron.pgn_update_interval_ms;
    victron["cvl_interval_ms"] = config.victron.cvl_update_interval_ms;
    victron["keepalive_interval_ms"] = config.victron.keepalive_interval_ms;
    victron["keepalive_timeout_ms"] = config.victron.keepalive_timeout_ms;

    JsonObject victronThresholds = victron.createNestedObject("thresholds");
    victronThresholds["undervoltage_v"] = config.victron.thresholds.undervoltage_v;
    victronThresholds["overvoltage_v"] = config.victron.thresholds.overvoltage_v;
    victronThresholds["overtemp_c"] = config.victron.thresholds.overtemp_c;
    victronThresholds["low_temp_charge_c"] = config.victron.thresholds.low_temp_charge_c;
    victronThresholds["imbalance_warn_mv"] = config.victron.thresholds.imbalance_warn_mv;
    victronThresholds["imbalance_alarm_mv"] = config.victron.thresholds.imbalance_alarm_mv;
    victronThresholds["soc_low_percent"] = config.victron.thresholds.soc_low_percent;
    victronThresholds["soc_high_percent"] = config.victron.thresholds.soc_high_percent;
    victronThresholds["derate_current_a"] = config.victron.thresholds.derate_current_a;

    JsonObject loggingObj = configObj.createNestedObject("logging");
    loggingObj["level"] = logLevelToLowercase(config.logging.log_level);
    loggingObj["serial_baudrate"] = config.logging.serial_baudrate;
    loggingObj["serial"] = config.logging.output_serial;
    loggingObj["web"] = config.logging.output_web;
    loggingObj["sd"] = config.logging.output_sd;
    loggingObj["syslog"] = config.logging.output_syslog;
    loggingObj["syslog_server"] = config.logging.syslog_server;
    loggingObj["log_uart_traffic"] = config.logging.log_uart_traffic;
    loggingObj["log_can_traffic"] = config.logging.log_can_traffic;
    loggingObj["log_cvl_changes"] = config.logging.log_cvl_changes;

    JsonObject system = configObj.createNestedObject("system");
    system["web_port"] = config.web_server.port;
    system["ws_update_interval"] = config.web_server.websocket_update_interval_ms;
    system["ws_max_clients"] = config.web_server.max_ws_clients;
    system["cors_enabled"] = config.web_server.enable_cors;
    system["auth_enabled"] = config.web_server.enable_auth;
    system["username"] = config.web_server.username;
    system["password"] = config.web_server.password;

    JsonObject tiny = configObj.createNestedObject("tinybms");
    tiny["poll_interval_ms"] = config.tinybms.poll_interval_ms;
    tiny["uart_retry_count"] = config.tinybms.uart_retry_count;
    tiny["uart_retry_delay_ms"] = config.tinybms.uart_retry_delay_ms;
    tiny["broadcast_expected"] = config.tinybms.broadcast_expected;

    JsonObject advanced = configObj.createNestedObject("advanced");
    advanced["enable_spiffs"] = config.advanced.enable_spiffs;
    advanced["enable_ota"] = config.advanced.enable_ota;
    advanced["watchdog_timeout_s"] = config.advanced.watchdog_timeout_s;
    advanced["stack_size_bytes"] = config.advanced.stack_size_bytes;

    xSemaphoreGive(configMutex);
    return true;
}

void buildVictronCanMappingDocument(JsonDocument& doc) {
    const auto& definitions = getVictronPgnDefinitions();
    doc["success"] = true;
    doc["loaded"] = !definitions.empty();
    JsonArray pgns = doc.createNestedArray("pgns");

    for (const auto& def : definitions) {
        JsonObject pgnObj = pgns.createNestedObject();
        pgnObj["pgn"] = String("0x") + String(def.pgn, HEX);
        if (def.name.length() > 0) {
            pgnObj["name"] = def.name;
        }

        JsonArray fields = pgnObj.createNestedArray("fields");
        for (const auto& field : def.fields) {
            JsonObject fieldObj = fields.createNestedObject();
            fieldObj["name"] = field.name;
            fieldObj["byte_offset"] = field.byte_offset;
            if (field.length > 0) {
                fieldObj["length"] = field.length;
            }
            if (field.bit_length > 0) {
                fieldObj["bit_offset"] = field.bit_offset;
                fieldObj["bit_length"] = field.bit_length;
            }
            fieldObj["encoding"] = victronFieldEncodingToString(field.encoding);

            JsonObject sourceObj = fieldObj.createNestedObject("source");
            sourceObj["type"] = victronValueSourceTypeToString(field.source.type);
            if (!field.source.identifier.isEmpty()) {
                if (field.source.type == VictronValueSourceType::LiveData) {
                    sourceObj["field"] = tinyLiveDataFieldToString(field.source.live_field);
                } else {
                    sourceObj["id"] = field.source.identifier;
                }
            }
            if (field.source.type == VictronValueSourceType::Constant) {
                sourceObj["value"] = field.source.constant;
            }

            JsonObject convObj = fieldObj.createNestedObject("conversion");
            convObj["gain"] = field.conversion.gain;
            convObj["offset"] = field.conversion.offset;
            convObj["round"] = field.conversion.round;
            if (field.conversion.has_min) {
                convObj["min"] = field.conversion.min_value;
            }
            if (field.conversion.has_max) {
                convObj["max"] = field.conversion.max_value;
            }
        }
    }
}

bool applySettingsPayload(const JsonObjectConst& settings,
                          bool persist,
                          String& errorMessage,
                          bool& loggingLevelChanged,
                          LogLevel& newLogLevel) {
    if (settings.isNull()) {
        errorMessage = "invalid_payload";
        return false;
    }

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        errorMessage = "config_mutex_timeout";
        return false;
    }

    loggingLevelChanged = false;

    auto updateWifi = [](JsonObjectConst wifiObj) {
        if (wifiObj.isNull()) return;
        if (wifiObj.containsKey("mode")) config.wifi.mode = wifiObj["mode"].as<String>();
        if (wifiObj.containsKey("sta_ssid")) config.wifi.sta_ssid = wifiObj["sta_ssid"].as<String>();
        else if (wifiObj.containsKey("ssid")) config.wifi.sta_ssid = wifiObj["ssid"].as<String>();
        if (wifiObj.containsKey("sta_password")) config.wifi.sta_password = wifiObj["sta_password"].as<String>();
        else if (wifiObj.containsKey("password")) config.wifi.sta_password = wifiObj["password"].as<String>();
        if (wifiObj.containsKey("sta_hostname")) config.wifi.sta_hostname = wifiObj["sta_hostname"].as<String>();
        else if (wifiObj.containsKey("hostname")) config.wifi.sta_hostname = wifiObj["hostname"].as<String>();
        if (wifiObj.containsKey("sta_ip_mode")) config.wifi.sta_ip_mode = wifiObj["sta_ip_mode"].as<String>();
        if (wifiObj.containsKey("sta_static_ip")) config.wifi.sta_static_ip = wifiObj["sta_static_ip"].as<String>();
        if (wifiObj.containsKey("sta_gateway")) config.wifi.sta_gateway = wifiObj["sta_gateway"].as<String>();
        if (wifiObj.containsKey("sta_subnet")) config.wifi.sta_subnet = wifiObj["sta_subnet"].as<String>();

        if (wifiObj.containsKey("ap_fallback")) {
            if (wifiObj["ap_fallback"].is<bool>()) {
                config.wifi.ap_fallback.enabled = wifiObj["ap_fallback"].as<bool>();
            } else {
                JsonObjectConst apObj = wifiObj["ap_fallback"].as<JsonObjectConst>();
                if (!apObj.isNull()) {
                    if (apObj.containsKey("enabled")) config.wifi.ap_fallback.enabled = apObj["enabled"].as<bool>();
                    if (apObj.containsKey("ssid")) config.wifi.ap_fallback.ssid = apObj["ssid"].as<String>();
                    if (apObj.containsKey("password")) config.wifi.ap_fallback.password = apObj["password"].as<String>();
                    if (apObj.containsKey("channel")) config.wifi.ap_fallback.channel = apObj["channel"].as<int>();
                }
            }
        }
        if (wifiObj.containsKey("ap_ssid")) config.wifi.ap_fallback.ssid = wifiObj["ap_ssid"].as<String>();
        if (wifiObj.containsKey("ap_password")) config.wifi.ap_fallback.password = wifiObj["ap_password"].as<String>();
        if (wifiObj.containsKey("ap_channel")) config.wifi.ap_fallback.channel = wifiObj["ap_channel"].as<int>();
    };

    auto updateHardware = [](JsonObjectConst hwObj) {
        if (hwObj.isNull()) return;
        JsonObjectConst uartObj = hwObj["uart"].as<JsonObjectConst>();
        if (!uartObj.isNull()) {
            if (uartObj.containsKey("rx_pin")) config.hardware.uart.rx_pin = uartObj["rx_pin"].as<int>();
            if (uartObj.containsKey("tx_pin")) config.hardware.uart.tx_pin = uartObj["tx_pin"].as<int>();
            if (uartObj.containsKey("baudrate")) config.hardware.uart.baudrate = uartObj["baudrate"].as<int>();
            if (uartObj.containsKey("timeout_ms")) config.hardware.uart.timeout_ms = uartObj["timeout_ms"].as<int>();
        }
        if (hwObj.containsKey("uart_rx_pin")) config.hardware.uart.rx_pin = hwObj["uart_rx_pin"].as<int>();
        if (hwObj.containsKey("uart_tx_pin")) config.hardware.uart.tx_pin = hwObj["uart_tx_pin"].as<int>();
        if (hwObj.containsKey("uart_baudrate")) config.hardware.uart.baudrate = hwObj["uart_baudrate"].as<int>();
        if (hwObj.containsKey("uart_timeout_ms")) config.hardware.uart.timeout_ms = hwObj["uart_timeout_ms"].as<int>();

        JsonObjectConst canObj = hwObj["can"].as<JsonObjectConst>();
        if (!canObj.isNull()) {
            if (canObj.containsKey("tx_pin")) config.hardware.can.tx_pin = canObj["tx_pin"].as<int>();
            if (canObj.containsKey("rx_pin")) config.hardware.can.rx_pin = canObj["rx_pin"].as<int>();
            if (canObj.containsKey("bitrate")) config.hardware.can.bitrate = canObj["bitrate"].as<uint32_t>();
            if (canObj.containsKey("mode")) config.hardware.can.mode = canObj["mode"].as<String>();
            if (canObj.containsKey("termination")) config.hardware.can.termination = canObj["termination"].as<bool>();
        }
        if (hwObj.containsKey("can_tx_pin")) config.hardware.can.tx_pin = hwObj["can_tx_pin"].as<int>();
        if (hwObj.containsKey("can_rx_pin")) config.hardware.can.rx_pin = hwObj["can_rx_pin"].as<int>();
        if (hwObj.containsKey("can_bitrate")) config.hardware.can.bitrate = hwObj["can_bitrate"].as<uint32_t>();
        if (hwObj.containsKey("can_mode")) config.hardware.can.mode = hwObj["can_mode"].as<String>();
        if (hwObj.containsKey("can_termination")) config.hardware.can.termination = hwObj["can_termination"].as<bool>();
    };

    auto updateCvl = [](JsonObjectConst cvlObj) {
        if (cvlObj.isNull()) return;
        if (cvlObj.containsKey("enabled")) config.cvl.enabled = cvlObj["enabled"].as<bool>();
        if (cvlObj.containsKey("bulk_transition_soc")) config.cvl.bulk_soc_threshold = cvlObj["bulk_transition_soc"].as<float>();
        if (cvlObj.containsKey("transition_float_soc")) config.cvl.transition_soc_threshold = cvlObj["transition_float_soc"].as<float>();
        if (cvlObj.containsKey("float_soc_threshold")) config.cvl.float_soc_threshold = cvlObj["float_soc_threshold"].as<float>();
        if (cvlObj.containsKey("float_exit_soc")) config.cvl.float_exit_soc = cvlObj["float_exit_soc"].as<float>();
        if (cvlObj.containsKey("float_approach_offset")) config.cvl.float_approach_offset_mv = cvlObj["float_approach_offset"].as<float>();
        if (cvlObj.containsKey("float_offset")) config.cvl.float_offset_mv = cvlObj["float_offset"].as<float>();
        if (cvlObj.containsKey("minimum_ccl_in_float_a")) config.cvl.minimum_ccl_in_float_a = cvlObj["minimum_ccl_in_float_a"].as<float>();
        if (cvlObj.containsKey("imbalance_trigger_mv")) config.cvl.imbalance_hold_threshold_mv = cvlObj["imbalance_trigger_mv"].as<uint16_t>();
        if (cvlObj.containsKey("imbalance_release_mv")) config.cvl.imbalance_release_threshold_mv = cvlObj["imbalance_release_mv"].as<uint16_t>();
        if (cvlObj.containsKey("imbalance_offset")) config.cvl.minimum_ccl_in_float_a = cvlObj["imbalance_offset"].as<float>();
    };

    if (settings.containsKey("wifi")) {
        updateWifi(settings["wifi"].as<JsonObjectConst>());
    }

    if (settings.containsKey("hardware")) {
        updateHardware(settings["hardware"].as<JsonObjectConst>());
    }

    if (settings.containsKey("tinybms")) {
        JsonObjectConst tinyObj = settings["tinybms"].as<JsonObjectConst>();
        if (!tinyObj.isNull()) {
            if (tinyObj.containsKey("poll_interval_ms")) config.tinybms.poll_interval_ms = tinyObj["poll_interval_ms"].as<uint32_t>();
            if (tinyObj.containsKey("uart_retry_count")) config.tinybms.uart_retry_count = tinyObj["uart_retry_count"].as<uint8_t>();
            if (tinyObj.containsKey("uart_retry_delay_ms")) config.tinybms.uart_retry_delay_ms = tinyObj["uart_retry_delay_ms"].as<uint32_t>();
            if (tinyObj.containsKey("broadcast_expected")) config.tinybms.broadcast_expected = tinyObj["broadcast_expected"].as<bool>();
        }
    }

    if (settings.containsKey("cvl")) {
        updateCvl(settings["cvl"].as<JsonObjectConst>());
    }
    if (settings.containsKey("cvl_algorithm")) {
        updateCvl(settings["cvl_algorithm"].as<JsonObjectConst>());
    }

    if (settings.containsKey("victron")) {
        JsonObjectConst vicObj = settings["victron"].as<JsonObjectConst>();
        if (!vicObj.isNull()) {
            if (vicObj.containsKey("manufacturer")) config.victron.manufacturer_name = vicObj["manufacturer"].as<String>();
            if (vicObj.containsKey("manufacturer_name")) config.victron.manufacturer_name = vicObj["manufacturer_name"].as<String>();
            if (vicObj.containsKey("battery_name")) config.victron.battery_name = vicObj["battery_name"].as<String>();
            if (vicObj.containsKey("pgn_interval_ms")) config.victron.pgn_update_interval_ms = vicObj["pgn_interval_ms"].as<uint32_t>();
            if (vicObj.containsKey("cvl_interval_ms")) config.victron.cvl_update_interval_ms = vicObj["cvl_interval_ms"].as<uint32_t>();
            if (vicObj.containsKey("keepalive_interval_ms")) config.victron.keepalive_interval_ms = vicObj["keepalive_interval_ms"].as<uint32_t>();
            if (vicObj.containsKey("keepalive_timeout_ms")) config.victron.keepalive_timeout_ms = vicObj["keepalive_timeout_ms"].as<uint32_t>();

            JsonObjectConst thObj = vicObj["thresholds"].as<JsonObjectConst>();
            if (!thObj.isNull()) {
                if (thObj.containsKey("undervoltage_v")) config.victron.thresholds.undervoltage_v = thObj["undervoltage_v"].as<float>();
                if (thObj.containsKey("overvoltage_v")) config.victron.thresholds.overvoltage_v = thObj["overvoltage_v"].as<float>();
                if (thObj.containsKey("overtemp_c")) config.victron.thresholds.overtemp_c = thObj["overtemp_c"].as<float>();
                if (thObj.containsKey("low_temp_charge_c")) config.victron.thresholds.low_temp_charge_c = thObj["low_temp_charge_c"].as<float>();
                if (thObj.containsKey("imbalance_warn_mv")) config.victron.thresholds.imbalance_warn_mv = thObj["imbalance_warn_mv"].as<uint16_t>();
                if (thObj.containsKey("imbalance_alarm_mv")) config.victron.thresholds.imbalance_alarm_mv = thObj["imbalance_alarm_mv"].as<uint16_t>();
                if (thObj.containsKey("soc_low_percent")) config.victron.thresholds.soc_low_percent = thObj["soc_low_percent"].as<float>();
                if (thObj.containsKey("soc_high_percent")) config.victron.thresholds.soc_high_percent = thObj["soc_high_percent"].as<float>();
                if (thObj.containsKey("derate_current_a")) config.victron.thresholds.derate_current_a = thObj["derate_current_a"].as<float>();
            }
        }
    }

    if (settings.containsKey("logging")) {
        JsonObjectConst loggingObj = settings["logging"].as<JsonObjectConst>();
        if (!loggingObj.isNull()) {
            if (loggingObj.containsKey("serial_baudrate")) config.logging.serial_baudrate = loggingObj["serial_baudrate"].as<uint32_t>();
            if (loggingObj.containsKey("level")) {
                newLogLevel = parseLogLevelFromString(loggingObj["level"].as<String>());
                config.logging.log_level = newLogLevel;
                loggingLevelChanged = true;
            }
            if (loggingObj.containsKey("log_level")) {
                newLogLevel = parseLogLevelFromString(loggingObj["log_level"].as<String>());
                config.logging.log_level = newLogLevel;
                loggingLevelChanged = true;
            }
            if (loggingObj.containsKey("serial")) config.logging.output_serial = loggingObj["serial"].as<bool>();
            if (loggingObj.containsKey("web")) config.logging.output_web = loggingObj["web"].as<bool>();
            if (loggingObj.containsKey("sd")) config.logging.output_sd = loggingObj["sd"].as<bool>();
            if (loggingObj.containsKey("syslog")) config.logging.output_syslog = loggingObj["syslog"].as<bool>();
            if (loggingObj.containsKey("syslog_server")) config.logging.syslog_server = loggingObj["syslog_server"].as<String>();
            if (loggingObj.containsKey("log_uart_traffic")) config.logging.log_uart_traffic = loggingObj["log_uart_traffic"].as<bool>();
            if (loggingObj.containsKey("log_can_traffic")) config.logging.log_can_traffic = loggingObj["log_can_traffic"].as<bool>();
            if (loggingObj.containsKey("log_cvl_changes")) config.logging.log_cvl_changes = loggingObj["log_cvl_changes"].as<bool>();
        }
    }

    if (settings.containsKey("system")) {
        JsonObjectConst systemObj = settings["system"].as<JsonObjectConst>();
        if (!systemObj.isNull()) {
            if (systemObj.containsKey("web_port")) config.web_server.port = systemObj["web_port"].as<uint16_t>();
            if (systemObj.containsKey("ws_update_interval")) config.web_server.websocket_update_interval_ms = systemObj["ws_update_interval"].as<uint32_t>();
            if (systemObj.containsKey("ws_max_clients")) config.web_server.max_ws_clients = systemObj["ws_max_clients"].as<uint8_t>();
            if (systemObj.containsKey("cors_enabled")) config.web_server.enable_cors = systemObj["cors_enabled"].as<bool>();
            if (systemObj.containsKey("auth_enabled")) config.web_server.enable_auth = systemObj["auth_enabled"].as<bool>();
            if (systemObj.containsKey("username")) config.web_server.username = systemObj["username"].as<String>();
            if (systemObj.containsKey("password")) config.web_server.password = systemObj["password"].as<String>();
        }
    }

    if (settings.containsKey("web_server")) {
        JsonObjectConst webObj = settings["web_server"].as<JsonObjectConst>();
        if (!webObj.isNull()) {
            if (webObj.containsKey("port")) config.web_server.port = webObj["port"].as<uint16_t>();
            if (webObj.containsKey("websocket_update_interval_ms")) config.web_server.websocket_update_interval_ms = webObj["websocket_update_interval_ms"].as<uint32_t>();
            if (webObj.containsKey("enable_cors")) config.web_server.enable_cors = webObj["enable_cors"].as<bool>();
            if (webObj.containsKey("enable_auth")) config.web_server.enable_auth = webObj["enable_auth"].as<bool>();
            if (webObj.containsKey("username")) config.web_server.username = webObj["username"].as<String>();
            if (webObj.containsKey("password")) config.web_server.password = webObj["password"].as<String>();
            if (webObj.containsKey("max_ws_clients")) config.web_server.max_ws_clients = webObj["max_ws_clients"].as<uint8_t>();
        }
    }

    if (settings.containsKey("advanced")) {
        JsonObjectConst advObj = settings["advanced"].as<JsonObjectConst>();
        if (!advObj.isNull()) {
            if (advObj.containsKey("enable_spiffs")) config.advanced.enable_spiffs = advObj["enable_spiffs"].as<bool>();
            if (advObj.containsKey("enable_ota")) config.advanced.enable_ota = advObj["enable_ota"].as<bool>();
            if (advObj.containsKey("watchdog_timeout_s")) config.advanced.watchdog_timeout_s = advObj["watchdog_timeout_s"].as<uint32_t>();
            if (advObj.containsKey("stack_size_bytes")) config.advanced.stack_size_bytes = advObj["stack_size_bytes"].as<uint32_t>();
        }
    }

    if (settings.containsKey("watchdog_config")) {
        JsonObjectConst watchdogObj = settings["watchdog_config"].as<JsonObjectConst>();
        if (!watchdogObj.isNull()) {
            if (watchdogObj.containsKey("timeout_s")) config.advanced.watchdog_timeout_s = watchdogObj["timeout_s"].as<uint32_t>();
        }
    }

    xSemaphoreGive(configMutex);

    if (persist) {
        if (!config.save()) {
            errorMessage = "config_save_failed";
            return false;
        }
    }

    return true;
}

} // namespace

/**
 * @brief Register API routes
 */
void setupAPIRoutes(AsyncWebServer& server) {

    logger.log(LOG_INFO, "[API] Registering system API routes");

    // ===========================================
    // GET /api/status
    // ===========================================
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log(LOG_DEBUG, "[API] GET /api/status");
        request->send(200, "application/json", getStatusJSON());
    });

    // ===========================================
    // GET|PUT /api/config/system
    // ===========================================
    server.on("/api/config/system", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log(LOG_DEBUG, "[API] GET /api/config/system");
        request->send(200, "application/json", getSystemConfigJSON());
    });

    server.on("/api/config/system", HTTP_PUT, [](AsyncWebServerRequest *request) {
        logger.log(LOG_INFO, "[API] PUT /api/config/system");

        if (!request->hasArg("plain")) {
            logger.log(LOG_WARN, "[API] Missing JSON body");
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }

        StaticJsonDocument<2048> doc;
        DeserializationError error = deserializeJson(doc, request->arg("plain"));
        if (error) {
            logger.log(LOG_ERROR, String("[API] JSON parse error: ") + error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }

        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            logger.log(LOG_ERROR, String("[API] Failed to update config/system: ") + errorMessage);
            sendErrorResponse(request, 500, "Failed to update configuration", errorMessage.c_str());
            return;
        }

        if (loggingChanged) {
            logger.setLogLevel(newLevel);
        }

        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Configuration updated";
        sendJsonResponse(request, 200, resp);
    });
    // ===========================================
    // GET /api/memory
    // ===========================================
    server.on("/api/memory", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<256> doc;
        JsonObject memory = doc.createNestedObject("memory");
        memory["free_heap"] = ESP.getFreeHeap();
        memory["min_free_heap"] = ESP.getMinFreeHeap();
        memory["max_alloc_heap"] = ESP.getMaxAllocHeap();
    #ifdef BOARD_HAS_PSRAM
        memory["psram_free"] = ESP.getFreePsram();
    #endif
        doc["free_heap"] = memory["free_heap"];
        doc["min_free_heap"] = memory["min_free_heap"];
        doc["max_alloc_heap"] = memory["max_alloc_heap"];
    #ifdef BOARD_HAS_PSRAM
        doc["psram_free"] = memory["psram_free"];
    #endif
        doc["success"] = true;
        sendJsonResponse(request, 200, doc);
    });

    // ===========================================
    // GET /api/system
    // ===========================================
    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;
        doc["success"] = true;

        JsonObject wifi = doc.createNestedObject("wifi");
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            wifi["mode"] = config.wifi.mode;
            wifi["ssid"] = config.wifi.sta_ssid;
            wifi["hostname"] = config.wifi.sta_hostname;
            wifi["sta_ip_mode"] = config.wifi.sta_ip_mode;
            wifi["ap_ssid"] = config.wifi.ap_fallback.ssid;
            wifi["ap_channel"] = config.wifi.ap_fallback.channel;
            xSemaphoreGive(configMutex);
        } else {
            wifi["error"] = "config_mutex_timeout";
        }

        bool wifiConnected = WiFi.status() == WL_CONNECTED;
        wifi["connected"] = wifiConnected;
        wifi["ip"] = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        wifi["rssi"] = WiFi.RSSI();
        wifi["channel"] = wifiConnected ? WiFi.channel() : wifi["ap_channel"].as<int>();

        doc["uptime_s"] = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["min_free_heap"] = ESP.getMinFreeHeap();
        doc["spiffs_used"] = SPIFFS.usedBytes();
        doc["spiffs_total"] = SPIFFS.totalBytes();

        sendJsonResponse(request, 200, doc);
    });

    // ===========================================
    // GET /api/can/mapping
    // ===========================================
    server.on("/api/can/mapping", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<4096> doc;
        buildVictronCanMappingDocument(doc);
        sendJsonResponse(request, 200, doc);
    });

    // ===========================================
    // GET /api/config
    // ===========================================
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<1536> doc;
        JsonObject cfg = doc.createNestedObject("config");
        String errorMessage;
        if (!buildSettingsSnapshot(cfg, errorMessage)) {
            sendErrorResponse(request, 500, "Failed to build configuration", errorMessage.c_str());
            return;
        }
        doc["success"] = true;
        sendJsonResponse(request, 200, doc);
    });

    // ===========================================
    // POST /api/config/wifi
    // ===========================================
    server.on("/api/config/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, request->arg("plain"));
        if (error) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to update WiFi", errorMessage.c_str());
            return;
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "WiFi settings updated";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/wifi/test
    // ===========================================
    server.on("/api/wifi/test", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        String ssid = doc["ssid"].as<String>();
        StaticJsonDocument<256> resp;
        resp["success"] = false;
        resp["message"] = "Test not available";
        if (ssid.equals(config.wifi.sta_ssid) && WiFi.status() == WL_CONNECTED) {
            resp["success"] = true;
            resp["message"] = "Using active connection";
            resp["rssi"] = WiFi.RSSI();
        }
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // GET /api/hardware/test/uart
    // ===========================================
    server.on("/api/hardware/test/uart", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint16_t value = 0;
        bool ok = bridge.readTinyRegisters(0x0000, 1, &value);
        StaticJsonDocument<256> resp;
        resp["success"] = ok;
        if (ok) {
            resp["value"] = value;
            resp["message"] = "TinyBMS responded";
        } else {
            resp["message"] = "No response from TinyBMS";
        }
        sendJsonResponse(request, ok ? 200 : 503, resp);
    });

    // ===========================================
    // GET /api/hardware/test/can
    // ===========================================
    server.on("/api/hardware/test/can", HTTP_GET, [](AsyncWebServerRequest *request) {
        hal::CanStats stats = hal::HalManager::instance().can().getStats();
        bool ok = (stats.tx_success + stats.rx_success) > 0 && stats.bus_off_events == 0;
        StaticJsonDocument<256> resp;
        resp["success"] = ok;
        resp["tx_success"] = stats.tx_success;
        resp["rx_success"] = stats.rx_success;
        resp["tx_errors"] = stats.tx_errors;
        resp["rx_errors"] = stats.rx_errors;
        resp["bus_off_events"] = stats.bus_off_events;
        resp["message"] = ok ? "CAN bus active" : "No CAN activity detected";
        sendJsonResponse(request, ok ? 200 : 503, resp);
    });

    // ===========================================
    // POST /api/config/hardware
    // ===========================================
    server.on("/api/config/hardware", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to update hardware", errorMessage.c_str());
            return;
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Hardware settings updated";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // GET /api/config/cvl
    // ===========================================
    server.on("/api/config/cvl", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> doc;
        String errorMessage;
        JsonObject cfg = doc.createNestedObject("cvl");
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            cfg["enabled"] = config.cvl.enabled;
            cfg["bulk_transition_soc"] = config.cvl.bulk_soc_threshold;
            cfg["transition_float_soc"] = config.cvl.transition_soc_threshold;
            cfg["float_soc_threshold"] = config.cvl.float_soc_threshold;
            cfg["float_exit_soc"] = config.cvl.float_exit_soc;
            cfg["float_approach_offset"] = config.cvl.float_approach_offset_mv;
            cfg["float_offset"] = config.cvl.float_offset_mv;
            cfg["minimum_ccl_in_float_a"] = config.cvl.minimum_ccl_in_float_a;
            cfg["imbalance_trigger_mv"] = config.cvl.imbalance_hold_threshold_mv;
            cfg["imbalance_release_mv"] = config.cvl.imbalance_release_threshold_mv;
            xSemaphoreGive(configMutex);
            doc["success"] = true;
            sendJsonResponse(request, 200, doc);
        } else {
            sendErrorResponse(request, 500, "Failed to access configuration", "config_mutex_timeout");
        }
    });

    // ===========================================
    // POST /api/config/cvl
    // ===========================================
    server.on("/api/config/cvl", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to update CVL", errorMessage.c_str());
            return;
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "CVL settings updated";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/config/victron
    // ===========================================
    server.on("/api/config/victron", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<768> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to update Victron", errorMessage.c_str());
            return;
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Victron settings updated";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/logs/clear
    // ===========================================
    server.on("/api/logs/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool ok = logger.clearLogs();
        StaticJsonDocument<128> resp;
        resp["success"] = ok;
        resp["message"] = ok ? "Logs cleared" : "Failed to clear logs";
        sendJsonResponse(request, ok ? 200 : 500, resp);
    });

    // ===========================================
    // GET /api/logs/download
    // ===========================================
    server.on("/api/logs/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        String logs = logger.getLogs();
        StaticJsonDocument<256> resp;
        resp["success"] = logs.length() > 0;
        resp["logs"] = logs;
        if (logs.isEmpty()) {
            resp["message"] = "No logs available";
        }
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/config/logging
    // ===========================================
    server.on("/api/config/logging", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to update logging", errorMessage.c_str());
            return;
        }
        if (loggingChanged) {
            logger.setLogLevel(newLevel);
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Logging settings updated";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/config/import
    // ===========================================
    server.on("/api/config/import", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<1536> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(doc.as<JsonObjectConst>(), true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to import configuration", errorMessage.c_str());
            return;
        }
        if (loggingChanged) {
            logger.setLogLevel(newLevel);
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Configuration imported";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/config/reload
    // ===========================================
    server.on("/api/config/reload", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (config.begin()) {
            StaticJsonDocument<128> resp;
            resp["success"] = true;
            resp["message"] = "Configuration reloaded";
            sendJsonResponse(request, 200, resp);
        } else {
            sendErrorResponse(request, 500, "Failed to reload configuration", "reload_failed");
        }
    });

    // ===========================================
    // POST /api/config/save
    // ===========================================
    server.on("/api/config/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }
        StaticJsonDocument<1536> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }
        JsonObjectConst settings = doc["settings"].as<JsonObjectConst>();
        bool loggingChanged = false;
        LogLevel newLevel = logger.getLevel();
        String errorMessage;
        if (!applySettingsPayload(settings, true, errorMessage, loggingChanged, newLevel)) {
            sendErrorResponse(request, 500, "Failed to save configuration", errorMessage.c_str());
            return;
        }
        if (loggingChanged) {
            logger.setLogLevel(newLevel);
        }
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Configuration saved";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // POST /api/system/restart
    // ===========================================
    server.on("/api/system/restart", HTTP_POST, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Restarting";
        sendJsonResponse(request, 200, resp);
        if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Watchdog.feed();
            xSemaphoreGive(feedMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP.restart();
    });

    // ===========================================
    // POST /api/config/reset
    // ===========================================
    server.on("/api/config/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool removed = SPIFFS.remove("/config.json");
        StaticJsonDocument<128> resp;
        resp["success"] = removed;
        resp["message"] = removed ? "Configuration reset" : "No configuration file";
        sendJsonResponse(request, removed ? 200 : 500, resp);
    });

    // ===========================================
    // POST /api/system/factory-reset
    // ===========================================
    server.on("/api/system/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        bool configRemoved = SPIFFS.remove("/config.json");
        bool logsRemoved = SPIFFS.remove("/logs.txt");
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["config_removed"] = configRemoved;
        resp["logs_removed"] = logsRemoved;
        resp["message"] = "Factory reset requested";
        sendJsonResponse(request, 200, resp);
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP.restart();
    });

    // ===========================================
    // POST /api/stats/reset
    // ===========================================
    server.on("/api/stats/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        eventBus.resetStats();
        StaticJsonDocument<128> resp;
        resp["success"] = true;
        resp["message"] = "Statistics reset";
        sendJsonResponse(request, 200, resp);
    });

    // ===========================================
    // GET /api/statistics
    // ===========================================
    server.on("/api/statistics", HTTP_GET, [](AsyncWebServerRequest *request) {
        tinybms::event::BusStatistics stats = eventBus.statistics();

        StaticJsonDocument<768> doc;
        doc["success"] = true;

        JsonObject data = doc.createNestedObject("data");
        if (request->hasParam("period")) {
            data["period"] = request->getParam("period")->value();
        }
        if (request->hasParam("start")) {
            data["start"] = request->getParam("start")->value();
        }
        if (request->hasParam("end")) {
            data["end"] = request->getParam("end")->value();
        }

        JsonObject kpis = data.createNestedObject("kpis");
        kpis["avg_soc"] = 0;
        kpis["soc_trend"] = 0;
        kpis["energy_charged"] = 0;
        kpis["energy_discharged"] = 0;
        kpis["avg_temp"] = 0;
        kpis["temp_trend"] = 0;
        kpis["total_cycles"] = 0;
        kpis["cycles_delta"] = 0;

        JsonObject history = data.createNestedObject("history");
        history.createNestedArray("soc");
        history.createNestedArray("voltage");
        history.createNestedArray("current");
        history.createNestedArray("power");
        history.createNestedArray("temperature");
        history.createNestedArray("timestamps");

        JsonArray events = data.createNestedArray("events");
        (void)events;  // Placeholder to keep structure consistent

        JsonObject eventBus = data.createNestedObject("event_bus");
        eventBus["total_events_published"] = stats.total_published;
        eventBus["total_events_dispatched"] = stats.total_delivered;
        eventBus["subscriber_count"] = stats.subscriber_count;
        eventBus["queue_overruns"] = 0;
        eventBus["dispatch_errors"] = 0;
        eventBus["current_queue_depth"] = 0;

        sendJsonResponse(request, 200, doc);
    });

    // ===========================================
    // GET|PUT /api/watchdog
    // ===========================================
    server.on("/api/watchdog", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log(LOG_DEBUG, "[API] GET /api/watchdog status");

        StaticJsonDocument<512> doc;
        doc["enabled"] = Watchdog.isEnabled();
        doc["timeout_ms"] = Watchdog.getTimeout();
        doc["time_since_last_feed_ms"] = Watchdog.getTimeSinceLastFeed();
        doc["feed_count"] = Watchdog.getFeedCount();
        doc["health_ok"] = Watchdog.checkHealth();
        doc["last_reset_reason"] = Watchdog.getResetReasonString();
        doc["time_until_timeout_ms"] = Watchdog.getTimeUntilTimeout();

        String output;
        serializeJson(doc, output);
        request->send(200, "application/json", output);
    });

    server.on("/api/watchdog", HTTP_PUT, [](AsyncWebServerRequest *request) {
        logger.log(LOG_INFO, "[API] PUT /api/watchdog");

        if (!request->hasArg("plain")) {
            logger.log(LOG_WARN, "[API] Missing body in watchdog update");
            request->send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, request->arg("plain"));
        if (error) {
            logger.log(LOG_ERROR, String("[API] JSON parse error (watchdog): ") + error.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            logger.log(LOG_ERROR, "[API] Failed to acquire feed mutex for watchdog");
            request->send(500, "application/json", "{\"error\":\"Failed to access watchdog\"}");
            return;
        }

        bool updated = false;

        if (doc.containsKey("enabled")) {
            bool enable = doc["enabled"].as<bool>();
            if (enable && !Watchdog.isEnabled()) {
                updated = Watchdog.enable();
                logger.log(LOG_INFO, "[API] Watchdog enabled via API");
            } else if (!enable && Watchdog.isEnabled()) {
                updated = Watchdog.disable();
                logger.log(LOG_INFO, "[API] Watchdog disabled via API");
            }
        }

        if (doc.containsKey("timeout_ms")) {
            uint32_t timeout_ms = doc["timeout_ms"].as<uint32_t>();
            if (timeout_ms >= WATCHDOG_MIN_TIMEOUT && timeout_ms <= WATCHDOG_MAX_TIMEOUT) {
                Watchdog.disable();
                updated = Watchdog.begin(timeout_ms);
                logger.log(LOG_INFO, "[API] Watchdog timeout updated to " + String(timeout_ms) + " ms");
            } else {
                xSemaphoreGive(feedMutex);
                logger.log(LOG_WARN, "[API] Invalid timeout value for watchdog");
                request->send(400, "application/json", "{\"error\":\"Invalid timeout value\"}");
                return;
            }
        }

        if (updated) {
            if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                config.advanced.watchdog_timeout_s = Watchdog.getTimeout() / 1000;
                xSemaphoreGive(configMutex);
                config.save();
            }
        }

        xSemaphoreGive(feedMutex);
        request->send(200, "application/json", "{\"status\":\"Watchdog updated\"}");
    });

    // ===========================================
    // POST /api/reboot
    // ===========================================
    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log(LOG_WARNING, "[API] Reboot requested via API");

        request->send(200, "application/json", "{\"status\":\"Rebooting\"}");
        if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Watchdog.feed();
            xSemaphoreGive(feedMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        logger.log(LOG_WARNING, "[API] System rebooting...");
        ESP.restart();
    });
}