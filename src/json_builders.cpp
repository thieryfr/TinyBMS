/**
 * @file json_builders.cpp
 * @brief JSON response builders for API endpoints with Logging + FreeRTOS
 * @version 1.2 - Integration of logging, mutex safety
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "shared_data.h"
#include "logger.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "json_builders.h"
#include "tinybms_victron_bridge.h"
#include "event_bus.h"  // Phase 6: Event Bus integration

// External globals
extern TinyBMS_Victron_Bridge bridge;
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern SemaphoreHandle_t configMutex;
extern Logger logger;
extern EventBus& eventBus;  // Phase 6: Event Bus instance

// ============================================================================
// STATUS JSON
// ============================================================================
String getStatusJSON() {
    StaticJsonDocument<1664> doc;  // +status message payload

    TinyBMS_LiveData data;
    // Phase 6: Use Event Bus cache instead of legacy queue
    if (!eventBus.getLatestLiveData(data)) {
        logger.log(LOG_DEBUG, "[JSON] No cached data, using bridge.getLiveData()");
        data = bridge.getLiveData();
    }

    JsonObject live = doc.createNestedObject("live_data");
    live["voltage"] = round(data.voltage * 100) / 100.0;
    live["current"] = round(data.current * 10) / 10.0;
    live["soc_percent"] = round(data.soc_percent * 10) / 10.0;
    live["soh_percent"] = round(data.soh_percent * 10) / 10.0;
    live["temperature"] = data.temperature;
    live["min_cell_mv"] = data.min_cell_mv;
    live["max_cell_mv"] = data.max_cell_mv;
    live["cell_imbalance_mv"] = data.cell_imbalance_mv;
    live["balancing_bits"] = data.balancing_bits;
    live["online_status"] = data.online_status;

    // Statistics
    JsonObject stats = doc.createNestedObject("stats");
    stats["cvl_current_v"] = round(bridge.stats.cvl_current_v * 10) / 10.0;
    stats["cvl_state"] = bridge.stats.cvl_state;

    const char* cvl_state_names[] = {"BULK", "TRANSITION", "FLOAT_APPROACH", "FLOAT", "IMBALANCE_HOLD"};
    if (bridge.stats.cvl_state < 5)
        stats["cvl_state_name"] = cvl_state_names[bridge.stats.cvl_state];
    else
        stats["cvl_state_name"] = "UNKNOWN";

    stats["can_tx_count"] = bridge.stats.can_tx_count;
    stats["can_rx_count"] = bridge.stats.can_rx_count;
    stats["can_tx_errors"] = bridge.stats.can_tx_errors;
    stats["can_rx_errors"] = bridge.stats.can_rx_errors;
    stats["can_bus_off_count"] = bridge.stats.can_bus_off_count;
    stats["can_queue_overflows"] = bridge.stats.can_queue_overflows;
    stats["uart_errors"] = bridge.stats.uart_errors;
    stats["uart_success_count"] = bridge.stats.uart_success_count;
    stats["uart_timeouts"] = bridge.stats.uart_timeouts;
    stats["uart_crc_errors"] = bridge.stats.uart_crc_errors;
    stats["uart_retry_count"] = bridge.stats.uart_retry_count;
    stats["victron_keepalive_ok"] = bridge.stats.victron_keepalive_ok;
    stats["ccl_limit_a"] = round(bridge.stats.ccl_limit_a * 10) / 10.0;
    stats["dcl_limit_a"] = round(bridge.stats.dcl_limit_a * 10) / 10.0;

    // Watchdog info
    JsonObject wdt = doc.createNestedObject("watchdog");
    wdt["enabled"] = Watchdog.isEnabled();
    wdt["timeout_ms"] = Watchdog.getTimeout();
    wdt["time_since_last_feed_ms"] = Watchdog.getTimeSinceLastFeed();
    wdt["feed_count"] = Watchdog.getFeedCount();
    wdt["health_ok"] = Watchdog.checkHealth();
    wdt["last_reset_reason"] = Watchdog.getResetReasonString();
    wdt["time_until_timeout_ms"] = Watchdog.getTimeUntilTimeout();

    doc["uptime_ms"] = xTaskGetTickCount() * portTICK_PERIOD_MS;

    BusEvent status_event;
    if (eventBus.getLatest(EVENT_STATUS_MESSAGE, status_event)) {
        JsonObject status = doc.createNestedObject("status_message");
        status["message"] = status_event.data.status.message;
        status["level"] = status_event.data.status.level;
        static const char* level_names[] = {"info", "notice", "warning", "error"};
        if (status_event.data.status.level < (sizeof(level_names) / sizeof(level_names[0]))) {
            status["level_name"] = level_names[status_event.data.status.level];
        }
        status["source_id"] = status_event.source_id;
        status["timestamp_ms"] = status_event.timestamp_ms;
    }

    String output;
    serializeJson(doc, output);

    logger.log(LOG_DEBUG, "[JSON] Built /api/status payload (" + String(output.length()) + " bytes)");
    return output;
}

// ============================================================================
// TINYBMS CONFIG JSON
// ============================================================================
String getConfigJSON() {
    StaticJsonDocument<512> doc;
    
    const TinyBMS_Config& cfg = bridge.getConfig();
    doc["fully_charged_voltage_mv"] = cfg.fully_charged_voltage_mv;
    doc["fully_discharged_voltage_mv"] = cfg.fully_discharged_voltage_mv;
    doc["charge_finished_current_ma"] = cfg.charge_finished_current_ma;
    doc["battery_capacity_ah"] = cfg.battery_capacity_ah_scaled / 100.0;
    doc["cell_count"] = cfg.cell_count;
    doc["overvoltage_cutoff_mv"] = cfg.overvoltage_cutoff_mv;
    doc["undervoltage_cutoff_mv"] = cfg.undervoltage_cutoff_mv;
    doc["discharge_overcurrent_a"] = cfg.discharge_overcurrent_a;
    doc["charge_overcurrent_a"] = cfg.charge_overcurrent_a;
    doc["overheat_cutoff_c"] = cfg.overheat_cutoff_c / 10.0;
    doc["low_temp_charge_cutoff_c"] = cfg.low_temp_charge_cutoff_c / 10.0;

    String output;
    serializeJson(doc, output);

    logger.log(LOG_DEBUG, "[JSON] Built /api/config/tinybms payload (" + String(output.length()) + " bytes)");
    return output;
}

// ============================================================================
// SYSTEM CONFIG JSON
// ============================================================================
String getSystemConfigJSON() {
    StaticJsonDocument<3072> doc;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "[JSON] Failed to acquire config mutex");
        doc["error"] = "Failed to access configuration";
        String out;
        serializeJson(doc, out);
        return out;
    }

    // WiFi Info
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["ssid"] = config.wifi.ssid;
    wifi["password"] = config.wifi.password;
    wifi["hostname"] = config.wifi.hostname;
    wifi["connected"] = WiFi.status() == WL_CONNECTED;
    wifi["ip"] = WiFi.status() == WL_CONNECTED ?
                  WiFi.localIP().toString() : WiFi.softAPIP().toString();
    wifi["rssi"] = WiFi.RSSI();
    wifi["mode"] = WiFi.status() == WL_CONNECTED ? "STA" : "AP";

    JsonObject ap_fallback = wifi.createNestedObject("ap_fallback");
    ap_fallback["enabled"] = config.wifi.ap_fallback.enabled;
    ap_fallback["ssid"] = config.wifi.ap_fallback.ssid;
    ap_fallback["password"] = config.wifi.ap_fallback.password;

    // Hardware
    JsonObject hw = doc.createNestedObject("hardware");
    JsonObject uart = hw.createNestedObject("uart");
    uart["rx_pin"] = config.hardware.uart.rx_pin;
    uart["tx_pin"] = config.hardware.uart.tx_pin;
    uart["baudrate"] = config.hardware.uart.baudrate;
    uart["timeout_ms"] = config.hardware.uart.timeout_ms;

    JsonObject can = hw.createNestedObject("can");
    can["tx_pin"] = config.hardware.can.tx_pin;
    can["rx_pin"] = config.hardware.can.rx_pin;
    can["bitrate"] = config.hardware.can.bitrate;
    can["mode"] = config.hardware.can.mode;

    // TinyBMS
    JsonObject tiny = doc.createNestedObject("tinybms");
    tiny["poll_interval_ms"] = config.tinybms.poll_interval_ms;
    tiny["uart_retry_count"] = config.tinybms.uart_retry_count;
    tiny["uart_retry_delay_ms"] = config.tinybms.uart_retry_delay_ms;
    tiny["broadcast_expected"] = config.tinybms.broadcast_expected;

    // CVL Algorithm
    JsonObject cvl = doc.createNestedObject("cvl_algorithm");
    cvl["enabled"] = config.cvl.enabled;
    cvl["bulk_soc_threshold"] = config.cvl.bulk_soc_threshold;
    cvl["transition_soc_threshold"] = config.cvl.transition_soc_threshold;
    cvl["float_soc_threshold"] = config.cvl.float_soc_threshold;
    cvl["float_exit_soc"] = config.cvl.float_exit_soc;
    cvl["float_approach_offset_mv"] = config.cvl.float_approach_offset_mv;
    cvl["float_offset_mv"] = config.cvl.float_offset_mv;
    cvl["minimum_ccl_in_float_a"] = config.cvl.minimum_ccl_in_float_a;
    cvl["imbalance_hold_threshold_mv"] = config.cvl.imbalance_hold_threshold_mv;
    cvl["imbalance_release_threshold_mv"] = config.cvl.imbalance_release_threshold_mv;

    // Victron
    JsonObject victron = doc.createNestedObject("victron");
    victron["manufacturer_name"] = config.victron.manufacturer_name;
    victron["battery_name"] = config.victron.battery_name;
    victron["pgn_update_interval_ms"] = config.victron.pgn_update_interval_ms;
    victron["cvl_update_interval_ms"] = config.victron.cvl_update_interval_ms;
    victron["keepalive_interval_ms"] = config.victron.keepalive_interval_ms;
    victron["keepalive_timeout_ms"] = config.victron.keepalive_timeout_ms;

    JsonObject vic_th = victron.createNestedObject("thresholds");
    vic_th["undervoltage_v"] = config.victron.thresholds.undervoltage_v;
    vic_th["overvoltage_v"] = config.victron.thresholds.overvoltage_v;
    vic_th["overtemp_c"] = config.victron.thresholds.overtemp_c;
    vic_th["low_temp_charge_c"] = config.victron.thresholds.low_temp_charge_c;
    vic_th["imbalance_warn_mv"] = config.victron.thresholds.imbalance_warn_mv;
    vic_th["imbalance_alarm_mv"] = config.victron.thresholds.imbalance_alarm_mv;
    vic_th["soc_low_percent"] = config.victron.thresholds.soc_low_percent;
    vic_th["soc_high_percent"] = config.victron.thresholds.soc_high_percent;
    vic_th["derate_current_a"] = config.victron.thresholds.derate_current_a;

    // Web server
    JsonObject web = doc.createNestedObject("web_server");
    web["port"] = config.web_server.port;
    web["websocket_update_interval_ms"] = config.web_server.websocket_update_interval_ms;
    web["enable_cors"] = config.web_server.enable_cors;
    web["enable_auth"] = config.web_server.enable_auth;
    web["username"] = config.web_server.username;
    web["password"] = config.web_server.password;

    // Logging
    JsonObject logging = doc.createNestedObject("logging");
    logging["serial_baudrate"] = config.logging.serial_baudrate;
    switch (config.logging.log_level) {
        case LOG_ERROR:   logging["log_level"] = "ERROR"; break;
        case LOG_WARNING: logging["log_level"] = "WARNING"; break;
        case LOG_DEBUG:   logging["log_level"] = "DEBUG"; break;
        case LOG_INFO:
        default:          logging["log_level"] = "INFO"; break;
    }
    logging["log_uart_traffic"] = config.logging.log_uart_traffic;
    logging["log_can_traffic"] = config.logging.log_can_traffic;
    logging["log_cvl_changes"] = config.logging.log_cvl_changes;

    // Advanced
    JsonObject advanced = doc.createNestedObject("advanced");
    advanced["enable_spiffs"] = config.advanced.enable_spiffs;
    advanced["enable_ota"] = config.advanced.enable_ota;
    advanced["watchdog_timeout_s"] = config.advanced.watchdog_timeout_s;
    advanced["stack_size_bytes"] = config.advanced.stack_size_bytes;

    // Watchdog
    JsonObject wdt_cfg = doc.createNestedObject("watchdog_config");
    wdt_cfg["timeout_s"] = config.advanced.watchdog_timeout_s;
    wdt_cfg["enabled"] = Watchdog.isEnabled();

    // System
    doc["uptime_s"] = (xTaskGetTickCount() * portTICK_PERIOD_MS) / 1000;
    doc["free_heap"] = ESP.getFreeHeap();
    doc["config_loaded"] = config.isLoaded();
    doc["spiffs_used"] = SPIFFS.usedBytes();
    doc["spiffs_total"] = SPIFFS.totalBytes();

    xSemaphoreGive(configMutex);

    String output;
    serializeJson(doc, output);

    logger.log(LOG_DEBUG, "[JSON] Built /api/config/system payload (" + String(output.length()) + " bytes)");
    return output;
}