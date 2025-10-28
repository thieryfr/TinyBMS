#include "config_manager.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "logger.h"
#include "event_bus.h"

extern SemaphoreHandle_t configMutex;
extern Logger logger;
extern EventBus& eventBus;

ConfigManager::ConfigManager()
    : filename_("/config.json"), loaded_(false) {}

bool ConfigManager::begin(const char* filename) {
    filename_ = filename;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "Config load failed: could not acquire configMutex");
        return false;
    }

    if (!SPIFFS.begin(true)) {
        logger.log(LOG_ERROR, "SPIFFS mount failed");
        xSemaphoreGive(configMutex);
        return false;
    }

    if (!SPIFFS.exists(filename_)) {
        logger.log(LOG_WARNING, String("Config file not found (") + filename_ + "), using defaults");
        loaded_ = false;
        xSemaphoreGive(configMutex);
        return false;
    }

    File file = SPIFFS.open(filename_, "r");
    if (!file) {
        logger.log(LOG_ERROR, String("Failed to open config file: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    DynamicJsonDocument doc(6144);
    DeserializationError error = deserializeJson(doc, file);
    file.close();

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
    loadWebServerConfig(doc);
    loadLoggingConfig(doc);
    loadAdvancedConfig(doc);

    loaded_ = true;
    logger.log(LOG_INFO, "Configuration loaded successfully");

    printConfig();

    eventBus.publishConfigChange("*", "", "", SOURCE_ID_CONFIG_MANAGER);

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
    saveWebServerConfig(doc);
    saveLoggingConfig(doc);
    saveAdvancedConfig(doc);

    File file = SPIFFS.open(filename_, "w");
    if (!file) {
        logger.log(LOG_ERROR, String("Failed to open config file for writing: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        logger.log(LOG_ERROR, "Failed to write configuration to file");
        file.close();
        xSemaphoreGive(configMutex);
        return false;
    }

    file.close();
    logger.log(LOG_INFO, "Configuration saved successfully");

    eventBus.publishConfigChange("*", "", "", SOURCE_ID_CONFIG_MANAGER);

    xSemaphoreGive(configMutex);
    return true;
}

void ConfigManager::loadWiFiConfig(const JsonDocument& doc) {
    JsonObjectConst wifiObj = doc["wifi"].as<JsonObjectConst>();
    if (wifiObj.isNull()) return;

    wifi.ssid = wifiObj["ssid"] | wifi.ssid;
    wifi.password = wifiObj["password"] | wifi.password;
    wifi.hostname = wifiObj["hostname"] | wifi.hostname;

    JsonObjectConst apObj = wifiObj["ap_fallback"].as<JsonObjectConst>();
    if (!apObj.isNull()) {
        wifi.ap_fallback.enabled = apObj["enabled"] | wifi.ap_fallback.enabled;
        wifi.ap_fallback.ssid = apObj["ssid"] | wifi.ap_fallback.ssid;
        wifi.ap_fallback.password = apObj["password"] | wifi.ap_fallback.password;
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
    }
}

void ConfigManager::loadTinyBMSConfig(const JsonDocument& doc) {
    JsonObjectConst tinyObj = doc["tinybms"].as<JsonObjectConst>();
    if (tinyObj.isNull()) return;

    tinybms.poll_interval_ms = tinyObj["poll_interval_ms"] | tinybms.poll_interval_ms;
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
}

void ConfigManager::loadWebServerConfig(const JsonDocument& doc) {
    JsonObjectConst webObj = doc["web_server"].as<JsonObjectConst>();
    if (webObj.isNull()) return;

    web_server.port = webObj["port"] | web_server.port;
    web_server.websocket_update_interval_ms = webObj["websocket_update_interval_ms"] | web_server.websocket_update_interval_ms;
    web_server.enable_cors = webObj["enable_cors"] | web_server.enable_cors;
    web_server.enable_auth = webObj["enable_auth"] | web_server.enable_auth;
    web_server.username = webObj["username"] | web_server.username;
    web_server.password = webObj["password"] | web_server.password;
}

void ConfigManager::loadLoggingConfig(const JsonDocument& doc) {
    JsonObjectConst logObj = doc["logging"].as<JsonObjectConst>();
    if (logObj.isNull()) return;

    logging.serial_baudrate = logObj["serial_baudrate"] | logging.serial_baudrate;
    logging.log_uart_traffic = logObj["log_uart_traffic"] | logging.log_uart_traffic;
    logging.log_can_traffic = logObj["log_can_traffic"] | logging.log_can_traffic;
    logging.log_cvl_changes = logObj["log_cvl_changes"] | logging.log_cvl_changes;

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
    wifiObj["ssid"] = wifi.ssid;
    wifiObj["password"] = wifi.password;
    wifiObj["hostname"] = wifi.hostname;

    JsonObject apObj = wifiObj.createNestedObject("ap_fallback");
    apObj["enabled"] = wifi.ap_fallback.enabled;
    apObj["ssid"] = wifi.ap_fallback.ssid;
    apObj["password"] = wifi.ap_fallback.password;
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
}

void ConfigManager::saveTinyBMSConfig(JsonDocument& doc) const {
    JsonObject tinyObj = doc.createNestedObject("tinybms");
    tinyObj["poll_interval_ms"] = tinybms.poll_interval_ms;
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
}

void ConfigManager::saveWebServerConfig(JsonDocument& doc) const {
    JsonObject webObj = doc.createNestedObject("web_server");
    webObj["port"] = web_server.port;
    webObj["websocket_update_interval_ms"] = web_server.websocket_update_interval_ms;
    webObj["enable_cors"] = web_server.enable_cors;
    webObj["enable_auth"] = web_server.enable_auth;
    webObj["username"] = web_server.username;
    webObj["password"] = web_server.password;
}

void ConfigManager::saveLoggingConfig(JsonDocument& doc) const {
    JsonObject logObj = doc.createNestedObject("logging");
    logObj["serial_baudrate"] = logging.serial_baudrate;
    logObj["log_level"] = logLevelToString(logging.log_level);
    logObj["log_uart_traffic"] = logging.log_uart_traffic;
    logObj["log_can_traffic"] = logging.log_can_traffic;
    logObj["log_cvl_changes"] = logging.log_cvl_changes;
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
    logger.log(LOG_DEBUG, "WiFi: SSID=" + wifi.ssid + " Hostname=" + wifi.hostname);
    logger.log(LOG_DEBUG, "UART: RX=" + String(hardware.uart.rx_pin) +
                              " TX=" + String(hardware.uart.tx_pin) +
                              " Baud=" + String(hardware.uart.baudrate));
    logger.log(LOG_DEBUG, "CAN: RX=" + String(hardware.can.rx_pin) +
                              " TX=" + String(hardware.can.tx_pin) +
                              " Bitrate=" + String(hardware.can.bitrate));
    logger.log(LOG_DEBUG, "Victron keepalive timeout=" + String(victron.keepalive_timeout_ms) + "ms");
    logger.log(LOG_DEBUG, "Logging Level=" + String(logLevelToString(logging.log_level)));
}
