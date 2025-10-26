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

// External globals
extern TinyBMS_Victron_Bridge bridge;
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern QueueHandle_t liveDataQueue;
extern SemaphoreHandle_t configMutex;
extern Logger logger;

// ============================================================================
// STATUS JSON
// ============================================================================
String getStatusJSON() {
    StaticJsonDocument<1536> doc;  // 1.5KB pour inclure watchdog
    
    TinyBMS_LiveData data;
    if (xQueuePeek(liveDataQueue, &data, 0) != pdTRUE) {
        logger.log(LOG_DEBUG, "[JSON] liveDataQueue empty, using bridge.getLiveData()");
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
    stats["uart_errors"] = bridge.stats.uart_errors;
    stats["victron_keepalive_ok"] = bridge.stats.victron_keepalive_ok;

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
    StaticJsonDocument<1024> doc;

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
    wifi["connected"] = WiFi.status() == WL_CONNECTED;
    wifi["ip"] = WiFi.status() == WL_CONNECTED ?
                 WiFi.localIP().toString() : WiFi.softAPIP().toString();
    wifi["rssi"] = WiFi.RSSI();
    wifi["hostname"] = config.wifi.hostname;
    wifi["mode"] = WiFi.status() == WL_CONNECTED ? "STA" : "AP";

    // Hardware
    JsonObject hw = doc.createNestedObject("hardware");
    hw["uart_rx"] = config.hardware.uart.rx_pin;
    hw["uart_tx"] = config.hardware.uart.tx_pin;
    hw["uart_baudrate"] = config.hardware.uart.baudrate;
    hw["can_tx"] = config.hardware.can.tx_pin;
    hw["can_rx"] = config.hardware.can.rx_pin;
    hw["can_bitrate"] = config.hardware.can.bitrate;

    // CVL Algorithm
    JsonObject cvl = doc.createNestedObject("cvl_algorithm");
    cvl["enabled"] = config.cvl.enabled;
    cvl["bulk_threshold"] = config.cvl.bulk_soc_threshold;
    cvl["float_threshold"] = config.cvl.float_soc_threshold;
    cvl["float_exit"] = config.cvl.float_exit_soc;

    // Victron
    JsonObject victron = doc.createNestedObject("victron");
    victron["manufacturer"] = config.victron.manufacturer_name;
    victron["battery_name"] = config.victron.battery_name;
    victron["pgn_update_ms"] = config.victron.pgn_update_interval_ms;
    victron["cvl_update_ms"] = config.victron.cvl_update_interval_ms;

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