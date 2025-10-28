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
#include "tiny_read_mapping.h"

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
    StaticJsonDocument<2048> doc;  // Expanded to include comms + alarm metadata

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

    JsonArray registers = live.createNestedArray("registers");
    for (size_t i = 0; i < data.snapshotCount(); ++i) {
        const TinyRegisterSnapshot& snap = data.snapshotAt(i);
        JsonObject reg = registers.createNestedObject();
        reg["address"] = snap.address;
        reg["raw"] = snap.raw_value;
        reg["word_count"] = snap.raw_word_count;

        const TinyRegisterRuntimeBinding* binding = findTinyRegisterBinding(snap.address);
        if (binding && binding->value_type == TinyRegisterValueType::String && snap.has_text) {
            reg["value"] = snap.text_value;
        } else {
            float scaled_value = static_cast<float>(snap.raw_value);
            if (binding) {
                scaled_value = static_cast<float>(snap.raw_value) * binding->scale;
            }
            reg["value"] = scaled_value;
        }
        reg["valid"] = snap.raw_word_count > 0;
        if (snap.has_text) {
            reg["text"] = snap.text_value;
        }

        const TinyRegisterMetadata* meta = findTinyRegisterMetadata(snap.address);
        if (meta) {
            reg["name"] = meta->name;
            reg["unit"] = meta->unit;
            reg["type"] = tinyRegisterTypeToString(meta->type);
            if (meta->comment.length() > 0) {
                reg["comment"] = meta->comment;
            }
        } else if (binding) {
            reg["type"] = tinyRegisterTypeToString(binding->value_type);
            if (binding->fallback_name) {
                reg["name"] = binding->fallback_name;
            }
            if (binding->fallback_unit) {
                reg["unit"] = binding->fallback_unit;
            }
        } else {
            reg["type"] = tinyRegisterTypeToString(static_cast<TinyRegisterValueType>(snap.type));
        }
    }

    // Statistics
    JsonObject stats = doc.createNestedObject("stats");
    stats["cvl_current_v"] = round(bridge.stats.cvl_current_v * 10) / 10.0;
    stats["cvl_state"] = bridge.stats.cvl_state;

    const char* cvl_state_names[] = {"BULK", "TRANSITION", "FLOAT_APPROACH", "FLOAT", "IMBALANCE_HOLD"};
    if (bridge.stats.cvl_state < 5)
        stats["cvl_state_name"] = cvl_state_names[bridge.stats.cvl_state];
    else
        stats["cvl_state_name"] = "UNKNOWN";

    JsonObject can_stats = stats.createNestedObject("can");
    can_stats["tx_success"] = bridge.stats.can_tx_count;
    can_stats["rx_success"] = bridge.stats.can_rx_count;
    can_stats["tx_errors"] = bridge.stats.can_tx_errors;
    can_stats["rx_errors"] = bridge.stats.can_rx_errors;
    can_stats["bus_off_count"] = bridge.stats.can_bus_off_count;
    can_stats["rx_dropped"] = bridge.stats.can_queue_overflows;

    stats["can_tx_count"] = bridge.stats.can_tx_count;           // Backward compatibility
    stats["can_rx_count"] = bridge.stats.can_rx_count;
    stats["can_tx_errors"] = bridge.stats.can_tx_errors;
    stats["can_rx_errors"] = bridge.stats.can_rx_errors;
    stats["can_bus_off_count"] = bridge.stats.can_bus_off_count;
    stats["can_queue_overflows"] = bridge.stats.can_queue_overflows;

    JsonObject uart_stats = stats.createNestedObject("uart");
    uart_stats["success"] = bridge.stats.uart_success_count;
    uart_stats["errors"] = bridge.stats.uart_errors;
    uart_stats["timeouts"] = bridge.stats.uart_timeouts;
    uart_stats["crc_errors"] = bridge.stats.uart_crc_errors;
    uart_stats["retry_count"] = bridge.stats.uart_retry_count;

    stats["uart_errors"] = bridge.stats.uart_errors;
    stats["uart_success_count"] = bridge.stats.uart_success_count;
    stats["uart_timeouts"] = bridge.stats.uart_timeouts;
    stats["uart_crc_errors"] = bridge.stats.uart_crc_errors;
    stats["uart_retry_count"] = bridge.stats.uart_retry_count;

    JsonObject keepalive_stats = stats.createNestedObject("keepalive");
    keepalive_stats["ok"] = bridge.stats.victron_keepalive_ok;
    keepalive_stats["last_tx_ms"] = bridge.last_keepalive_tx_ms_;
    keepalive_stats["last_rx_ms"] = bridge.last_keepalive_rx_ms_;
    keepalive_stats["interval_ms"] = bridge.keepalive_interval_ms_;
    keepalive_stats["timeout_ms"] = bridge.keepalive_timeout_ms_;
    keepalive_stats["since_last_rx_ms"] = bridge.last_keepalive_rx_ms_ > 0 ?
                                           (millis() - bridge.last_keepalive_rx_ms_) : 0;

    stats["victron_keepalive_ok"] = bridge.stats.victron_keepalive_ok;
    stats["ccl_limit_a"] = round(bridge.stats.ccl_limit_a * 10) / 10.0;
    stats["dcl_limit_a"] = round(bridge.stats.dcl_limit_a * 10) / 10.0;

    EventBus::BusStats bus_stats{};
    eventBus.getStats(bus_stats);
    JsonObject bus = stats.createNestedObject("event_bus");
    bus["total_events_published"] = bus_stats.total_events_published;
    bus["total_events_dispatched"] = bus_stats.total_events_dispatched;
    bus["queue_overruns"] = bus_stats.queue_overruns;
    bus["dispatch_errors"] = bus_stats.dispatch_errors;
    bus["total_subscribers"] = bus_stats.total_subscribers;
    bus["current_queue_depth"] = bus_stats.current_queue_depth;

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

    auto appendAlarmEvent = [](JsonArray& arr, const BusEvent& evt, const char* type_label) {
        JsonObject alarm_obj = arr.createNestedObject();
        alarm_obj["event"] = type_label;
        alarm_obj["timestamp_ms"] = evt.timestamp_ms;
        alarm_obj["source_id"] = evt.source_id;
        alarm_obj["sequence"] = evt.sequence_number;

        const AlarmEvent& alarm = evt.data.alarm;
        alarm_obj["code"] = alarm.alarm_code;
        alarm_obj["severity"] = alarm.severity;
        static const char* severity_names[] = {"info", "warning", "error", "critical"};
        if (alarm.severity < (sizeof(severity_names) / sizeof(severity_names[0]))) {
            alarm_obj["severity_name"] = severity_names[alarm.severity];
        }
        alarm_obj["message"] = alarm.message;
        alarm_obj["value"] = alarm.value;
        alarm_obj["active"] = alarm.is_active;
    };

    JsonArray alarms = doc.createNestedArray("alarms");
    BusEvent alarm_event;
    bool active_alarm = false;
    if (eventBus.getLatest(EVENT_ALARM_RAISED, alarm_event)) {
        appendAlarmEvent(alarms, alarm_event, "raised");
        active_alarm |= alarm_event.data.alarm.is_active;
    }
    if (eventBus.getLatest(EVENT_ALARM_CLEARED, alarm_event)) {
        appendAlarmEvent(alarms, alarm_event, "cleared");
        active_alarm &= alarm_event.data.alarm.is_active;
    }
    if (eventBus.getLatest(EVENT_WARNING_RAISED, alarm_event)) {
        appendAlarmEvent(alarms, alarm_event, "warning");
    }

    doc["alarms_active"] = active_alarm;

    String output;
    serializeJson(doc, output);

    logger.log(LOG_DEBUG, "[JSON] Built /api/status payload (" + String(output.length()) + " bytes)");
    return output;
}

// ============================================================================
// TINYBMS CONFIG JSON
// ============================================================================
String getConfigJSON() {
    StaticJsonDocument<640> doc;

    doc["success"] = true;

    const TinyBMS_Config& cfg = bridge.getConfig();
    JsonObject config = doc.createNestedObject("config");
    config["fully_charged_voltage_mv"] = cfg.fully_charged_voltage_mv;
    config["fully_discharged_voltage_mv"] = cfg.fully_discharged_voltage_mv;
    config["charge_finished_current_ma"] = cfg.charge_finished_current_ma;
    config["battery_capacity_ah"] = cfg.battery_capacity_ah;
    config["cell_count"] = cfg.cell_count;
    config["overvoltage_cutoff_mv"] = cfg.overvoltage_cutoff_mv;
    config["undervoltage_cutoff_mv"] = cfg.undervoltage_cutoff_mv;
    config["discharge_overcurrent_a"] = cfg.discharge_overcurrent_a;
    config["charge_overcurrent_a"] = cfg.charge_overcurrent_a;
    config["overheat_cutoff_c"] = cfg.overheat_cutoff_c;
    config["low_temp_charge_cutoff_c"] = cfg.low_temp_charge_cutoff_c;

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
    wifi["connected"] = WiFi.status() == WL_CONNECTED;
    wifi["ip"] = WiFi.status() == WL_CONNECTED ?
                  WiFi.localIP().toString() : WiFi.softAPIP().toString();
    wifi["rssi"] = WiFi.RSSI();
    wifi["mode_active"] = WiFi.status() == WL_CONNECTED ? "STA" : "AP";
    wifi["ap_ssid"] = config.wifi.ap_fallback.ssid;
    wifi["ap_password"] = config.wifi.ap_fallback.password;
    wifi["ap_channel"] = config.wifi.ap_fallback.channel;
    wifi["ap_fallback"] = config.wifi.ap_fallback.enabled;

    JsonObject ap_fallback = wifi.createNestedObject("ap_fallback");
    ap_fallback["enabled"] = config.wifi.ap_fallback.enabled;
    ap_fallback["ssid"] = config.wifi.ap_fallback.ssid;
    ap_fallback["password"] = config.wifi.ap_fallback.password;
    ap_fallback["channel"] = config.wifi.ap_fallback.channel;

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