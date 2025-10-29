#include "config_manager.h"
#include <ArduinoJson.h>
#include "logger.h"
#include "event/event_bus_v2.h"
#include "event/event_types_v2.h"
#include "hal/hal_manager.h"
#include "hal/interfaces/ihal_storage.h"

#include <string>
#include <vector>
#include <cstring>

extern SemaphoreHandle_t configMutex;
extern Logger logger;

using tinybms::event::eventBus;
using tinybms::events::ConfigChanged;
using tinybms::events::EventSource;

namespace {

void publishConfigChange(const char* path, const char* old_value, const char* new_value) {
    ConfigChanged event{};
    event.metadata.source = EventSource::ConfigManager;
    if (path) {
        std::strncpy(event.change.config_path, path, sizeof(event.change.config_path) - 1);
    }
    event.change.config_path[sizeof(event.change.config_path) - 1] = '\0';

    if (old_value) {
        std::strncpy(event.change.old_value, old_value, sizeof(event.change.old_value) - 1);
    }
    event.change.old_value[sizeof(event.change.old_value) - 1] = '\0';

    if (new_value) {
        std::strncpy(event.change.new_value, new_value, sizeof(event.change.new_value) - 1);
    }
    event.change.new_value[sizeof(event.change.new_value) - 1] = '\0';
    eventBus.publish(event);
}

} // namespace

ConfigManager::ConfigManager()
    : filename_("/config.json"), loaded_(false) {}

bool ConfigManager::begin(const char* filename) {
    filename_ = filename;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "Config load failed: could not acquire configMutex");
        return false;
    }

    hal::IHalStorage& storage = hal::HalManager::instance().storage();

    if (!storage.exists(filename_)) {
        logger.log(LOG_WARNING, String("Config file not found (") + filename_ + "), using defaults");
        loaded_ = false;
        xSemaphoreGive(configMutex);
        return false;
    }

    auto file = storage.open(filename_, hal::StorageOpenMode::Read);
    if (!file || !file->isOpen()) {
        logger.log(LOG_ERROR, String("Failed to open config file: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    DynamicJsonDocument doc(6144);
    std::vector<uint8_t> buffer(file->size());
    size_t read = buffer.empty() ? 0 : file->read(buffer.data(), buffer.size());
    file->close();

    if (read == 0) {
        logger.log(LOG_ERROR, String("Config file empty: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    DeserializationError error = deserializeJson(doc, buffer.data(), read);

    if (error) {
        logger.log(LOG_ERROR, String("JSON parse error: ") + error.c_str());
        xSemaphoreGive(configMutex);
        return false;
    }

    loadWiFiConfig(doc);
    loadHardwareConfig(doc);
    loadTinyBMSConfig(doc);
    loadVictronConfig(doc);
    loadCVLConfig(doc);
    loadMqttConfig(doc);
    loadWebServerConfig(doc);
    loadLoggingConfig(doc);
    loadAdvancedConfig(doc);

    loaded_ = true;
    logger.log(LOG_INFO, "Configuration loaded successfully");

    printConfig();

    publishConfigChange("*", "", "");

    xSemaphoreGive(configMutex);
    return true;
}

bool ConfigManager::save() {
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "Config save failed: could not acquire configMutex");
        return false;
    }

    DynamicJsonDocument doc(6144);

    saveWiFiConfig(doc);
    saveHardwareConfig(doc);
    saveTinyBMSConfig(doc);
    saveVictronConfig(doc);
    saveCVLConfig(doc);
    saveMqttConfig(doc);
    saveWebServerConfig(doc);
    saveLoggingConfig(doc);
    saveAdvancedConfig(doc);

    auto file = storage.open(filename_, hal::StorageOpenMode::Write);
    if (!file || !file->isOpen()) {
        logger.log(LOG_ERROR, String("Failed to open config file for writing: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    std::string output;
    serializeJson(doc, output);
    file->write(reinterpret_cast<const uint8_t*>(output.data()), output.size());
    file->close();
    logger.log(LOG_INFO, "Configuration saved successfully");

    publishConfigChange("*", "", "");

    xSemaphoreGive(configMutex);
    return true;
}

void ConfigManager::loadWiFiConfig(const JsonDocument& doc) {
    JsonObjectConst wifiObj = doc["wifi"].as<JsonObjectConst>();
    if (wifiObj.isNull()) return;

    wifi.mode = wifiObj["mode"] | wifi.mode;
    wifi.sta_ssid = wifiObj["sta_ssid"] | wifiObj["ssid"] | wifi.sta_ssid;
    wifi.sta_password = wifiObj["sta_password"] | wifiObj["password"] | wifi.sta_password;
    wifi.sta_hostname = wifiObj["sta_hostname"] | wifiObj["hostname"] | wifi.sta_hostname;
    wifi.sta_ip_mode = wifiObj["sta_ip_mode"] | wifi.sta_ip_mode;
    wifi.sta_static_ip = wifiObj["sta_static_ip"] | wifi.sta_static_ip;
    wifi.sta_gateway = wifiObj["sta_gateway"] | wifi.sta_gateway;
    wifi.sta_subnet = wifiObj["sta_subnet"] | wifi.sta_subnet;

    JsonObjectConst apObj = wifiObj["ap_fallback"].as<JsonObjectConst>();
    if (!apObj.isNull()) {
        wifi.ap_fallback.enabled = apObj["enabled"] | wifi.ap_fallback.enabled;
        wifi.ap_fallback.ssid = apObj["ssid"] | wifi.ap_fallback.ssid;
        wifi.ap_fallback.password = apObj["password"] | wifi.ap_fallback.password;
        wifi.ap_fallback.channel = apObj["channel"] | wifi.ap_fallback.channel;
    }

    if (wifiObj.containsKey("ap_ssid")) {
        wifi.ap_fallback.ssid = wifiObj["ap_ssid"].as<String>();
    }
    if (wifiObj.containsKey("ap_password")) {
        wifi.ap_fallback.password = wifiObj["ap_password"].as<String>();
    }
    if (wifiObj.containsKey("ap_channel")) {
        wifi.ap_fallback.channel = wifiObj["ap_channel"].as<int>();
    }
    if (wifiObj.containsKey("ap_fallback") && wifiObj["ap_fallback"].is<bool>()) {
        wifi.ap_fallback.enabled = wifiObj["ap_fallback"].as<bool>();
    }
}

void ConfigManager::loadHardwareConfig(const JsonDocument& doc) {
    JsonObjectConst hwObj = doc["hardware"].as<JsonObjectConst>();
    if (hwObj.isNull()) return;

    JsonObjectConst uartObj = hwObj["uart"].as<JsonObjectConst>();
    if (!uartObj.isNull()) {
        hardware.uart.rx_pin = uartObj["rx_pin"] | hardware.uart.rx_pin;
        hardware.uart.tx_pin = uartObj["tx_pin"] | hardware.uart.tx_pin;
        hardware.uart.baudrate = uartObj["baudrate"] | hardware.uart.baudrate;
        hardware.uart.timeout_ms = uartObj["timeout_ms"] | hardware.uart.timeout_ms;
    }

    JsonObjectConst canObj = hwObj["can"].as<JsonObjectConst>();
    if (!canObj.isNull()) {
        hardware.can.tx_pin = canObj["tx_pin"] | hardware.can.tx_pin;
        hardware.can.rx_pin = canObj["rx_pin"] | hardware.can.rx_pin;
        hardware.can.bitrate = canObj["bitrate"] | hardware.can.bitrate;
        hardware.can.mode = canObj["mode"] | hardware.can.mode;
        hardware.can.termination = canObj["termination"] | hardware.can.termination;
    }
}

void ConfigManager::loadTinyBMSConfig(const JsonDocument& doc) {
    JsonObjectConst tinyObj = doc["tinybms"].as<JsonObjectConst>();
    if (tinyObj.isNull()) return;

    tinybms.poll_interval_ms = tinyObj["poll_interval_ms"] | tinybms.poll_interval_ms;
    tinybms.poll_interval_min_ms = tinyObj["poll_interval_min_ms"] | tinybms.poll_interval_min_ms;
    tinybms.poll_interval_max_ms = tinyObj["poll_interval_max_ms"] | tinybms.poll_interval_max_ms;
    tinybms.poll_backoff_step_ms = tinyObj["poll_backoff_step_ms"] | tinybms.poll_backoff_step_ms;
    tinybms.poll_recovery_step_ms = tinyObj["poll_recovery_step_ms"] | tinybms.poll_recovery_step_ms;
    tinybms.poll_latency_target_ms = tinyObj["poll_latency_target_ms"] | tinybms.poll_latency_target_ms;
    tinybms.poll_latency_slack_ms = tinyObj["poll_latency_slack_ms"] | tinybms.poll_latency_slack_ms;
    tinybms.poll_failure_threshold = tinyObj["poll_failure_threshold"] | tinybms.poll_failure_threshold;
    tinybms.poll_success_threshold = tinyObj["poll_success_threshold"] | tinybms.poll_success_threshold;
    tinybms.uart_retry_count = tinyObj["uart_retry_count"] | tinybms.uart_retry_count;
    tinybms.uart_retry_delay_ms = tinyObj["uart_retry_delay_ms"] | tinybms.uart_retry_delay_ms;
    tinybms.broadcast_expected = tinyObj["broadcast_expected"] | tinybms.broadcast_expected;
}

void ConfigManager::loadVictronConfig(const JsonDocument& doc) {
    JsonObjectConst vicObj = doc["victron"].as<JsonObjectConst>();
    if (vicObj.isNull()) return;

    victron.pgn_update_interval_ms = vicObj["pgn_update_interval_ms"] | victron.pgn_update_interval_ms;
    victron.cvl_update_interval_ms = vicObj["cvl_update_interval_ms"] | victron.cvl_update_interval_ms;
    victron.keepalive_interval_ms = vicObj["keepalive_interval_ms"] | victron.keepalive_interval_ms;
    victron.keepalive_timeout_ms = vicObj["keepalive_timeout_ms"] | victron.keepalive_timeout_ms;
    victron.manufacturer_name = vicObj["manufacturer_name"] | victron.manufacturer_name;
    victron.battery_name = vicObj["battery_name"] | victron.battery_name;

    JsonObjectConst thObj = vicObj["thresholds"].as<JsonObjectConst>();
    if (!thObj.isNull()) {
        victron.thresholds.undervoltage_v = thObj["undervoltage_v"] | victron.thresholds.undervoltage_v;
        victron.thresholds.overvoltage_v = thObj["overvoltage_v"] | victron.thresholds.overvoltage_v;
        victron.thresholds.overtemp_c = thObj["overtemp_c"] | victron.thresholds.overtemp_c;
        victron.thresholds.low_temp_charge_c = thObj["low_temp_charge_c"] | victron.thresholds.low_temp_charge_c;
        victron.thresholds.imbalance_warn_mv = thObj["imbalance_warn_mv"] | victron.thresholds.imbalance_warn_mv;
        victron.thresholds.imbalance_alarm_mv = thObj["imbalance_alarm_mv"] | victron.thresholds.imbalance_alarm_mv;
        victron.thresholds.soc_low_percent = thObj["soc_low_percent"] | victron.thresholds.soc_low_percent;
        victron.thresholds.soc_high_percent = thObj["soc_high_percent"] | victron.thresholds.soc_high_percent;
        victron.thresholds.derate_current_a = thObj["derate_current_a"] | victron.thresholds.derate_current_a;
    }
}

void ConfigManager::loadCVLConfig(const JsonDocument& doc) {
    JsonObjectConst cvlObj = doc["cvl_algorithm"].as<JsonObjectConst>();
    if (cvlObj.isNull()) return;

    cvl.enabled = cvlObj["enabled"] | cvl.enabled;
    cvl.bulk_soc_threshold = cvlObj["bulk_soc_threshold"] | cvl.bulk_soc_threshold;
    cvl.transition_soc_threshold = cvlObj["transition_soc_threshold"] | cvl.transition_soc_threshold;
    cvl.float_soc_threshold = cvlObj["float_soc_threshold"] | cvl.float_soc_threshold;
    cvl.float_exit_soc = cvlObj["float_exit_soc"] | cvl.float_exit_soc;
    cvl.float_approach_offset_mv = cvlObj["float_approach_offset_mv"] | cvl.float_approach_offset_mv;
    cvl.float_offset_mv = cvlObj["float_offset_mv"] | cvl.float_offset_mv;
    cvl.minimum_ccl_in_float_a = cvlObj["minimum_ccl_in_float_a"] | cvl.minimum_ccl_in_float_a;
    cvl.imbalance_hold_threshold_mv = cvlObj["imbalance_hold_threshold_mv"] | cvl.imbalance_hold_threshold_mv;
    cvl.imbalance_release_threshold_mv = cvlObj["imbalance_release_threshold_mv"] | cvl.imbalance_release_threshold_mv;
    cvl.series_cell_count = cvlObj["series_cell_count"] | cvl.series_cell_count;
    cvl.cell_max_voltage_v = cvlObj["cell_max_voltage_v"] | cvl.cell_max_voltage_v;
    cvl.cell_safety_threshold_v = cvlObj["cell_safety_threshold_v"] | cvl.cell_safety_threshold_v;
    cvl.cell_safety_release_v = cvlObj["cell_safety_release_v"] | cvl.cell_safety_release_v;
    cvl.cell_min_float_voltage_v = cvlObj["cell_min_float_voltage_v"] | cvl.cell_min_float_voltage_v;
    cvl.cell_protection_kp = cvlObj["cell_protection_kp"] | cvl.cell_protection_kp;
    cvl.dynamic_current_nominal_a = cvlObj["dynamic_current_nominal_a"] | cvl.dynamic_current_nominal_a;
    cvl.max_recovery_step_v = cvlObj["max_recovery_step_v"] | cvl.max_recovery_step_v;
    cvl.sustain_soc_entry_percent = cvlObj["sustain_soc_entry_percent"] | cvl.sustain_soc_entry_percent;
    cvl.sustain_soc_exit_percent = cvlObj["sustain_soc_exit_percent"] | cvl.sustain_soc_exit_percent;
    cvl.sustain_voltage_v = cvlObj["sustain_voltage_v"] | cvl.sustain_voltage_v;
    cvl.sustain_per_cell_voltage_v = cvlObj["sustain_per_cell_voltage_v"] | cvl.sustain_per_cell_voltage_v;
    cvl.sustain_ccl_limit_a = cvlObj["sustain_ccl_limit_a"] | cvl.sustain_ccl_limit_a;
    cvl.sustain_dcl_limit_a = cvlObj["sustain_dcl_limit_a"] | cvl.sustain_dcl_limit_a;
    cvl.imbalance_drop_per_mv = cvlObj["imbalance_drop_per_mv"] | cvl.imbalance_drop_per_mv;
    cvl.imbalance_drop_max_v = cvlObj["imbalance_drop_max_v"] | cvl.imbalance_drop_max_v;
}

void ConfigManager::loadMqttConfig(const JsonDocument& doc) {
    JsonObjectConst mqttObj = doc["mqtt"].as<JsonObjectConst>();
    if (mqttObj.isNull()) return;

    mqtt.enabled = mqttObj["enabled"] | mqtt.enabled;
    mqtt.uri = mqttObj["uri"] | mqtt.uri;
    mqtt.port = mqttObj["port"] | mqtt.port;
    mqtt.client_id = mqttObj["client_id"] | mqtt.client_id;
    mqtt.username = mqttObj["username"] | mqtt.username;
    mqtt.password = mqttObj["password"] | mqtt.password;
    mqtt.root_topic = mqttObj["root_topic"] | mqtt.root_topic;
    mqtt.clean_session = mqttObj["clean_session"] | mqtt.clean_session;
    mqtt.use_tls = mqttObj["use_tls"] | mqtt.use_tls;
    mqtt.server_certificate = mqttObj["server_certificate"] | mqtt.server_certificate;
    mqtt.keepalive_seconds = mqttObj["keepalive_seconds"] | mqtt.keepalive_seconds;
    mqtt.reconnect_interval_ms = mqttObj["reconnect_interval_ms"] | mqtt.reconnect_interval_ms;
    mqtt.default_qos = mqttObj["default_qos"] | mqtt.default_qos;
    mqtt.retain_by_default = mqttObj["retain_by_default"] | mqtt.retain_by_default;
}

void ConfigManager::loadWebServerConfig(const JsonDocument& doc) {
    JsonObjectConst webObj = doc["web_server"].as<JsonObjectConst>();
    if (webObj.isNull()) return;

    web_server.port = webObj["port"] | web_server.port;
    web_server.websocket_update_interval_ms = webObj["websocket_update_interval_ms"] | web_server.websocket_update_interval_ms;
    web_server.websocket_min_interval_ms = webObj["websocket_min_interval_ms"] | web_server.websocket_min_interval_ms;
    web_server.websocket_burst_window_ms = webObj["websocket_burst_window_ms"] | web_server.websocket_burst_window_ms;
    web_server.websocket_burst_max = webObj["websocket_burst_max"] | web_server.websocket_burst_max;
    web_server.websocket_max_payload_bytes = webObj["websocket_max_payload_bytes"] | web_server.websocket_max_payload_bytes;
    web_server.enable_cors = webObj["enable_cors"] | web_server.enable_cors;
    web_server.enable_auth = webObj["enable_auth"] | web_server.enable_auth;
    web_server.username = webObj["username"] | web_server.username;
    web_server.password = webObj["password"] | web_server.password;
    web_server.max_ws_clients = webObj["max_ws_clients"] | web_server.max_ws_clients;
}

void ConfigManager::loadLoggingConfig(const JsonDocument& doc) {
    JsonObjectConst logObj = doc["logging"].as<JsonObjectConst>();
    if (logObj.isNull()) return;

    logging.serial_baudrate = logObj["serial_baudrate"] | logging.serial_baudrate;
    logging.log_uart_traffic = logObj["log_uart_traffic"] | logging.log_uart_traffic;
    logging.log_can_traffic = logObj["log_can_traffic"] | logging.log_can_traffic;
    logging.log_cvl_changes = logObj["log_cvl_changes"] | logging.log_cvl_changes;
    logging.output_serial = logObj["output_serial"] | logging.output_serial;
    logging.output_web = logObj["output_web"] | logging.output_web;
    logging.output_sd = logObj["output_sd"] | logging.output_sd;
    logging.output_syslog = logObj["output_syslog"] | logging.output_syslog;
    logging.syslog_server = logObj["syslog_server"] | logging.syslog_server;

    if (logObj.containsKey("log_level")) {
        String lvl = logObj["log_level"].as<const char*>();
        logging.log_level = parseLogLevel(lvl);
    }
}

void ConfigManager::loadAdvancedConfig(const JsonDocument& doc) {
    JsonObjectConst advObj = doc["advanced"].as<JsonObjectConst>();
    if (advObj.isNull()) return;

    advanced.enable_spiffs = advObj["enable_spiffs"] | advanced.enable_spiffs;
    advanced.enable_ota = advObj["enable_ota"] | advanced.enable_ota;
    advanced.watchdog_timeout_s = advObj["watchdog_timeout_s"] | advanced.watchdog_timeout_s;
    advanced.stack_size_bytes = advObj["stack_size_bytes"] | advanced.stack_size_bytes;
}

void ConfigManager::saveWiFiConfig(JsonDocument& doc) const {
    JsonObject wifiObj = doc.createNestedObject("wifi");
    wifiObj["mode"] = wifi.mode;
    wifiObj["ssid"] = wifi.sta_ssid;
    wifiObj["sta_ssid"] = wifi.sta_ssid;
    wifiObj["password"] = wifi.sta_password;
    wifiObj["sta_password"] = wifi.sta_password;
    wifiObj["hostname"] = wifi.sta_hostname;
    wifiObj["sta_hostname"] = wifi.sta_hostname;
    wifiObj["sta_ip_mode"] = wifi.sta_ip_mode;
    wifiObj["sta_static_ip"] = wifi.sta_static_ip;
    wifiObj["sta_gateway"] = wifi.sta_gateway;
    wifiObj["sta_subnet"] = wifi.sta_subnet;
    wifiObj["ap_ssid"] = wifi.ap_fallback.ssid;
    wifiObj["ap_password"] = wifi.ap_fallback.password;
    wifiObj["ap_channel"] = wifi.ap_fallback.channel;
    wifiObj["ap_fallback"] = wifi.ap_fallback.enabled;

    JsonObject apObj = wifiObj.createNestedObject("ap_fallback");
    apObj["enabled"] = wifi.ap_fallback.enabled;
    apObj["ssid"] = wifi.ap_fallback.ssid;
    apObj["password"] = wifi.ap_fallback.password;
    apObj["channel"] = wifi.ap_fallback.channel;
}

void ConfigManager::saveHardwareConfig(JsonDocument& doc) const {
    JsonObject hwObj = doc.createNestedObject("hardware");

    JsonObject uartObj = hwObj.createNestedObject("uart");
    uartObj["rx_pin"] = hardware.uart.rx_pin;
    uartObj["tx_pin"] = hardware.uart.tx_pin;
    uartObj["baudrate"] = hardware.uart.baudrate;
    uartObj["timeout_ms"] = hardware.uart.timeout_ms;

    JsonObject canObj = hwObj.createNestedObject("can");
    canObj["tx_pin"] = hardware.can.tx_pin;
    canObj["rx_pin"] = hardware.can.rx_pin;
    canObj["bitrate"] = hardware.can.bitrate;
    canObj["mode"] = hardware.can.mode;
    canObj["termination"] = hardware.can.termination;
}

void ConfigManager::saveTinyBMSConfig(JsonDocument& doc) const {
    JsonObject tinyObj = doc.createNestedObject("tinybms");
    tinyObj["poll_interval_ms"] = tinybms.poll_interval_ms;
    tinyObj["poll_interval_min_ms"] = tinybms.poll_interval_min_ms;
    tinyObj["poll_interval_max_ms"] = tinybms.poll_interval_max_ms;
    tinyObj["poll_backoff_step_ms"] = tinybms.poll_backoff_step_ms;
    tinyObj["poll_recovery_step_ms"] = tinybms.poll_recovery_step_ms;
    tinyObj["poll_latency_target_ms"] = tinybms.poll_latency_target_ms;
    tinyObj["poll_latency_slack_ms"] = tinybms.poll_latency_slack_ms;
    tinyObj["poll_failure_threshold"] = tinybms.poll_failure_threshold;
    tinyObj["poll_success_threshold"] = tinybms.poll_success_threshold;
    tinyObj["uart_retry_count"] = tinybms.uart_retry_count;
    tinyObj["uart_retry_delay_ms"] = tinybms.uart_retry_delay_ms;
    tinyObj["broadcast_expected"] = tinybms.broadcast_expected;
}

void ConfigManager::saveVictronConfig(JsonDocument& doc) const {
    JsonObject vicObj = doc.createNestedObject("victron");
    vicObj["pgn_update_interval_ms"] = victron.pgn_update_interval_ms;
    vicObj["cvl_update_interval_ms"] = victron.cvl_update_interval_ms;
    vicObj["keepalive_interval_ms"] = victron.keepalive_interval_ms;
    vicObj["keepalive_timeout_ms"] = victron.keepalive_timeout_ms;
    vicObj["manufacturer_name"] = victron.manufacturer_name;
    vicObj["battery_name"] = victron.battery_name;

    JsonObject thObj = vicObj.createNestedObject("thresholds");
    thObj["undervoltage_v"] = victron.thresholds.undervoltage_v;
    thObj["overvoltage_v"] = victron.thresholds.overvoltage_v;
    thObj["overtemp_c"] = victron.thresholds.overtemp_c;
    thObj["low_temp_charge_c"] = victron.thresholds.low_temp_charge_c;
    thObj["imbalance_warn_mv"] = victron.thresholds.imbalance_warn_mv;
    thObj["imbalance_alarm_mv"] = victron.thresholds.imbalance_alarm_mv;
    thObj["soc_low_percent"] = victron.thresholds.soc_low_percent;
    thObj["soc_high_percent"] = victron.thresholds.soc_high_percent;
    thObj["derate_current_a"] = victron.thresholds.derate_current_a;
}

void ConfigManager::saveCVLConfig(JsonDocument& doc) const {
    JsonObject cvlObj = doc.createNestedObject("cvl_algorithm");
    cvlObj["enabled"] = cvl.enabled;
    cvlObj["bulk_soc_threshold"] = cvl.bulk_soc_threshold;
    cvlObj["transition_soc_threshold"] = cvl.transition_soc_threshold;
    cvlObj["float_soc_threshold"] = cvl.float_soc_threshold;
    cvlObj["float_exit_soc"] = cvl.float_exit_soc;
    cvlObj["float_approach_offset_mv"] = cvl.float_approach_offset_mv;
    cvlObj["float_offset_mv"] = cvl.float_offset_mv;
    cvlObj["minimum_ccl_in_float_a"] = cvl.minimum_ccl_in_float_a;
    cvlObj["imbalance_hold_threshold_mv"] = cvl.imbalance_hold_threshold_mv;
    cvlObj["imbalance_release_threshold_mv"] = cvl.imbalance_release_threshold_mv;
    cvlObj["series_cell_count"] = cvl.series_cell_count;
    cvlObj["cell_max_voltage_v"] = cvl.cell_max_voltage_v;
    cvlObj["cell_safety_threshold_v"] = cvl.cell_safety_threshold_v;
    cvlObj["cell_safety_release_v"] = cvl.cell_safety_release_v;
    cvlObj["cell_min_float_voltage_v"] = cvl.cell_min_float_voltage_v;
    cvlObj["cell_protection_kp"] = cvl.cell_protection_kp;
    cvlObj["dynamic_current_nominal_a"] = cvl.dynamic_current_nominal_a;
    cvlObj["max_recovery_step_v"] = cvl.max_recovery_step_v;
    cvlObj["sustain_soc_entry_percent"] = cvl.sustain_soc_entry_percent;
    cvlObj["sustain_soc_exit_percent"] = cvl.sustain_soc_exit_percent;
    cvlObj["sustain_voltage_v"] = cvl.sustain_voltage_v;
    cvlObj["sustain_per_cell_voltage_v"] = cvl.sustain_per_cell_voltage_v;
    cvlObj["sustain_ccl_limit_a"] = cvl.sustain_ccl_limit_a;
    cvlObj["sustain_dcl_limit_a"] = cvl.sustain_dcl_limit_a;
    cvlObj["imbalance_drop_per_mv"] = cvl.imbalance_drop_per_mv;
    cvlObj["imbalance_drop_max_v"] = cvl.imbalance_drop_max_v;
}

void ConfigManager::saveMqttConfig(JsonDocument& doc) const {
    JsonObject mqttObj = doc.createNestedObject("mqtt");
    mqttObj["enabled"] = mqtt.enabled;
    mqttObj["uri"] = mqtt.uri;
    mqttObj["port"] = mqtt.port;
    mqttObj["client_id"] = mqtt.client_id;
    mqttObj["username"] = mqtt.username;
    mqttObj["password"] = mqtt.password;
    mqttObj["root_topic"] = mqtt.root_topic;
    mqttObj["clean_session"] = mqtt.clean_session;
    mqttObj["use_tls"] = mqtt.use_tls;
    mqttObj["server_certificate"] = mqtt.server_certificate;
    mqttObj["keepalive_seconds"] = mqtt.keepalive_seconds;
    mqttObj["reconnect_interval_ms"] = mqtt.reconnect_interval_ms;
    mqttObj["default_qos"] = mqtt.default_qos;
    mqttObj["retain_by_default"] = mqtt.retain_by_default;
}

void ConfigManager::saveWebServerConfig(JsonDocument& doc) const {
    JsonObject webObj = doc.createNestedObject("web_server");
    webObj["port"] = web_server.port;
    webObj["websocket_update_interval_ms"] = web_server.websocket_update_interval_ms;
    webObj["websocket_min_interval_ms"] = web_server.websocket_min_interval_ms;
    webObj["websocket_burst_window_ms"] = web_server.websocket_burst_window_ms;
    webObj["websocket_burst_max"] = web_server.websocket_burst_max;
    webObj["websocket_max_payload_bytes"] = web_server.websocket_max_payload_bytes;
    webObj["enable_cors"] = web_server.enable_cors;
    webObj["enable_auth"] = web_server.enable_auth;
    webObj["username"] = web_server.username;
    webObj["password"] = web_server.password;
    webObj["max_ws_clients"] = web_server.max_ws_clients;
}

void ConfigManager::saveLoggingConfig(JsonDocument& doc) const {
    JsonObject logObj = doc.createNestedObject("logging");
    logObj["serial_baudrate"] = logging.serial_baudrate;
    logObj["log_level"] = logLevelToString(logging.log_level);
    logObj["log_uart_traffic"] = logging.log_uart_traffic;
    logObj["log_can_traffic"] = logging.log_can_traffic;
    logObj["log_cvl_changes"] = logging.log_cvl_changes;
    logObj["output_serial"] = logging.output_serial;
    logObj["output_web"] = logging.output_web;
    logObj["output_sd"] = logging.output_sd;
    logObj["output_syslog"] = logging.output_syslog;
    logObj["syslog_server"] = logging.syslog_server;
}

void ConfigManager::saveAdvancedConfig(JsonDocument& doc) const {
    JsonObject advObj = doc.createNestedObject("advanced");
    advObj["enable_spiffs"] = advanced.enable_spiffs;
    advObj["enable_ota"] = advanced.enable_ota;
    advObj["watchdog_timeout_s"] = advanced.watchdog_timeout_s;
    advObj["stack_size_bytes"] = advanced.stack_size_bytes;
}

LogLevel ConfigManager::parseLogLevel(const String& level) const {
    String lvl = level;
    lvl.toUpperCase();
    if (lvl == "ERROR") return LOG_ERROR;
    if (lvl == "WARNING" || lvl == "WARN") return LOG_WARNING;
    if (lvl == "DEBUG") return LOG_DEBUG;
    return LOG_INFO;
}

const char* ConfigManager::logLevelToString(LogLevel level) const {
    switch (level) {
        case LOG_ERROR:   return "ERROR";
        case LOG_WARNING: return "WARNING";
        case LOG_DEBUG:   return "DEBUG";
        case LOG_INFO:
        default:          return "INFO";
    }
}

void ConfigManager::printConfig() const {
    logger.log(LOG_DEBUG, "=== CONFIG LOADED ===");
    logger.log(LOG_DEBUG, "WiFi: SSID=" + wifi.sta_ssid + " Hostname=" + wifi.sta_hostname);
    logger.log(LOG_DEBUG, "UART: RX=" + String(hardware.uart.rx_pin) +
                              " TX=" + String(hardware.uart.tx_pin) +
                              " Baud=" + String(hardware.uart.baudrate));
    logger.log(LOG_DEBUG, "CAN: RX=" + String(hardware.can.rx_pin) +
                              " TX=" + String(hardware.can.tx_pin) +
                              " Bitrate=" + String(hardware.can.bitrate));
    logger.log(LOG_DEBUG, "Victron keepalive timeout=" + String(victron.keepalive_timeout_ms) + "ms");
    logger.log(LOG_DEBUG, String("MQTT: enabled=") + (mqtt.enabled ? "true" : "false") +
                               ", uri=" + mqtt.uri +
                               ", root=" + mqtt.root_topic);
    logger.log(LOG_DEBUG, "Logging Level=" + String(logLevelToString(logging.log_level)));
}
