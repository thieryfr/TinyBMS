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

// External globals
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t feedMutex;
extern Logger logger;

// External functions
extern String getStatusJSON();
extern String getSystemConfigJSON();

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
            request->send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, request->arg("plain"));
        if (error) {
            logger.log(LOG_ERROR, String("[API] JSON parse error: ") + error.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        bool configLockTaken = false;
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            logger.log(LOG_ERROR, "[API] Failed to acquire config mutex");
            request->send(500, "application/json", "{\"error\":\"Failed to access config\"}");
            return;
        }
        configLockTaken = true;

        // WiFi settings
        if (doc.containsKey("wifi")) {
            logger.log(LOG_DEBUG, "[API] Updating WiFi config");
            JsonObjectConst wifiObj = doc["wifi"].as<JsonObjectConst>();
            if (!wifiObj.isNull()) {
                if (wifiObj.containsKey("ssid")) config.wifi.ssid = wifiObj["ssid"].as<String>();
                if (wifiObj.containsKey("password")) config.wifi.password = wifiObj["password"].as<String>();
                if (wifiObj.containsKey("hostname")) config.wifi.hostname = wifiObj["hostname"].as<String>();

                JsonObjectConst apObj = wifiObj["ap_fallback"].as<JsonObjectConst>();
                if (!apObj.isNull()) {
                    if (apObj.containsKey("enabled")) config.wifi.ap_fallback.enabled = apObj["enabled"].as<bool>();
                    if (apObj.containsKey("ssid")) config.wifi.ap_fallback.ssid = apObj["ssid"].as<String>();
                    if (apObj.containsKey("password")) config.wifi.ap_fallback.password = apObj["password"].as<String>();
                }
            }
        }

        // Hardware settings
        if (doc.containsKey("hardware")) {
            logger.log(LOG_DEBUG, "[API] Updating hardware config");
            JsonObjectConst hardwareObj = doc["hardware"].as<JsonObjectConst>();
            if (!hardwareObj.isNull()) {
                JsonObjectConst uartObj = hardwareObj["uart"].as<JsonObjectConst>();
                if (!uartObj.isNull()) {
                    if (uartObj.containsKey("rx_pin")) config.hardware.uart.rx_pin = uartObj["rx_pin"].as<int>();
                    if (uartObj.containsKey("tx_pin")) config.hardware.uart.tx_pin = uartObj["tx_pin"].as<int>();
                    if (uartObj.containsKey("baudrate")) config.hardware.uart.baudrate = uartObj["baudrate"].as<int>();
                    if (uartObj.containsKey("timeout_ms")) config.hardware.uart.timeout_ms = uartObj["timeout_ms"].as<int>();
                }

                JsonObjectConst canObj = hardwareObj["can"].as<JsonObjectConst>();
                if (!canObj.isNull()) {
                    if (canObj.containsKey("tx_pin")) config.hardware.can.tx_pin = canObj["tx_pin"].as<int>();
                    if (canObj.containsKey("rx_pin")) config.hardware.can.rx_pin = canObj["rx_pin"].as<int>();
                    if (canObj.containsKey("bitrate")) config.hardware.can.bitrate = canObj["bitrate"].as<uint32_t>();
                    if (canObj.containsKey("mode")) config.hardware.can.mode = canObj["mode"].as<String>();
                }
            }
        }

        // TinyBMS settings
        if (doc.containsKey("tinybms")) {
            logger.log(LOG_DEBUG, "[API] Updating TinyBMS config");
            JsonObjectConst tinyObj = doc["tinybms"].as<JsonObjectConst>();
            if (!tinyObj.isNull()) {
                if (tinyObj.containsKey("poll_interval_ms")) config.tinybms.poll_interval_ms = tinyObj["poll_interval_ms"].as<uint32_t>();
                if (tinyObj.containsKey("uart_retry_count")) config.tinybms.uart_retry_count = tinyObj["uart_retry_count"].as<uint8_t>();
                if (tinyObj.containsKey("uart_retry_delay_ms")) config.tinybms.uart_retry_delay_ms = tinyObj["uart_retry_delay_ms"].as<uint32_t>();
                if (tinyObj.containsKey("broadcast_expected")) config.tinybms.broadcast_expected = tinyObj["broadcast_expected"].as<bool>();
            }
        }

        // CVL Algorithm
        if (doc.containsKey("cvl_algorithm")) {
            logger.log(LOG_DEBUG, "[API] Updating CVL algorithm config");
            JsonObjectConst cvlObj = doc["cvl_algorithm"].as<JsonObjectConst>();
            if (!cvlObj.isNull()) {
                if (cvlObj.containsKey("enabled")) config.cvl.enabled = cvlObj["enabled"].as<bool>();
                if (cvlObj.containsKey("bulk_soc_threshold")) config.cvl.bulk_soc_threshold = cvlObj["bulk_soc_threshold"].as<float>();
                if (cvlObj.containsKey("transition_soc_threshold")) config.cvl.transition_soc_threshold = cvlObj["transition_soc_threshold"].as<float>();
                if (cvlObj.containsKey("float_soc_threshold")) config.cvl.float_soc_threshold = cvlObj["float_soc_threshold"].as<float>();
                if (cvlObj.containsKey("float_exit_soc")) config.cvl.float_exit_soc = cvlObj["float_exit_soc"].as<float>();
                if (cvlObj.containsKey("float_approach_offset_mv")) config.cvl.float_approach_offset_mv = cvlObj["float_approach_offset_mv"].as<float>();
                if (cvlObj.containsKey("float_offset_mv")) config.cvl.float_offset_mv = cvlObj["float_offset_mv"].as<float>();
                if (cvlObj.containsKey("minimum_ccl_in_float_a")) config.cvl.minimum_ccl_in_float_a = cvlObj["minimum_ccl_in_float_a"].as<float>();
                if (cvlObj.containsKey("imbalance_hold_threshold_mv")) config.cvl.imbalance_hold_threshold_mv = cvlObj["imbalance_hold_threshold_mv"].as<uint16_t>();
                if (cvlObj.containsKey("imbalance_release_threshold_mv")) config.cvl.imbalance_release_threshold_mv = cvlObj["imbalance_release_threshold_mv"].as<uint16_t>();
            }
        }

        // Victron settings
        if (doc.containsKey("victron")) {
            logger.log(LOG_DEBUG, "[API] Updating Victron config");
            JsonObjectConst victronObj = doc["victron"].as<JsonObjectConst>();
            if (!victronObj.isNull()) {
                if (victronObj.containsKey("manufacturer_name")) config.victron.manufacturer_name = victronObj["manufacturer_name"].as<String>();
                if (victronObj.containsKey("battery_name")) config.victron.battery_name = victronObj["battery_name"].as<String>();
                if (victronObj.containsKey("pgn_update_interval_ms")) config.victron.pgn_update_interval_ms = victronObj["pgn_update_interval_ms"].as<uint32_t>();
                if (victronObj.containsKey("cvl_update_interval_ms")) config.victron.cvl_update_interval_ms = victronObj["cvl_update_interval_ms"].as<uint32_t>();
                if (victronObj.containsKey("keepalive_interval_ms")) config.victron.keepalive_interval_ms = victronObj["keepalive_interval_ms"].as<uint32_t>();
                if (victronObj.containsKey("keepalive_timeout_ms")) config.victron.keepalive_timeout_ms = victronObj["keepalive_timeout_ms"].as<uint32_t>();

                JsonObjectConst thresholdsObj = victronObj["thresholds"].as<JsonObjectConst>();
                if (!thresholdsObj.isNull()) {
                    if (thresholdsObj.containsKey("undervoltage_v")) config.victron.thresholds.undervoltage_v = thresholdsObj["undervoltage_v"].as<float>();
                    if (thresholdsObj.containsKey("overvoltage_v")) config.victron.thresholds.overvoltage_v = thresholdsObj["overvoltage_v"].as<float>();
                    if (thresholdsObj.containsKey("overtemp_c")) config.victron.thresholds.overtemp_c = thresholdsObj["overtemp_c"].as<float>();
                    if (thresholdsObj.containsKey("low_temp_charge_c")) config.victron.thresholds.low_temp_charge_c = thresholdsObj["low_temp_charge_c"].as<float>();
                    if (thresholdsObj.containsKey("imbalance_warn_mv")) config.victron.thresholds.imbalance_warn_mv = thresholdsObj["imbalance_warn_mv"].as<uint16_t>();
                    if (thresholdsObj.containsKey("imbalance_alarm_mv")) config.victron.thresholds.imbalance_alarm_mv = thresholdsObj["imbalance_alarm_mv"].as<uint16_t>();
                    if (thresholdsObj.containsKey("soc_low_percent")) config.victron.thresholds.soc_low_percent = thresholdsObj["soc_low_percent"].as<float>();
                    if (thresholdsObj.containsKey("soc_high_percent")) config.victron.thresholds.soc_high_percent = thresholdsObj["soc_high_percent"].as<float>();
                    if (thresholdsObj.containsKey("derate_current_a")) config.victron.thresholds.derate_current_a = thresholdsObj["derate_current_a"].as<float>();
                }
            }
        }

        // Web server settings
        if (doc.containsKey("web_server")) {
            logger.log(LOG_DEBUG, "[API] Updating web server config");
            JsonObjectConst webObj = doc["web_server"].as<JsonObjectConst>();
            if (!webObj.isNull()) {
                if (webObj.containsKey("port")) config.web_server.port = webObj["port"].as<uint16_t>();
                if (webObj.containsKey("websocket_update_interval_ms")) config.web_server.websocket_update_interval_ms = webObj["websocket_update_interval_ms"].as<uint32_t>();
                if (webObj.containsKey("enable_cors")) config.web_server.enable_cors = webObj["enable_cors"].as<bool>();
                if (webObj.containsKey("enable_auth")) config.web_server.enable_auth = webObj["enable_auth"].as<bool>();
                if (webObj.containsKey("username")) config.web_server.username = webObj["username"].as<String>();
                if (webObj.containsKey("password")) config.web_server.password = webObj["password"].as<String>();
            }
        }

        // Logging settings
        if (doc.containsKey("logging")) {
            logger.log(LOG_DEBUG, "[API] Updating logging config");
            JsonObjectConst loggingObj = doc["logging"].as<JsonObjectConst>();
            if (!loggingObj.isNull()) {
                if (loggingObj.containsKey("serial_baudrate")) config.logging.serial_baudrate = loggingObj["serial_baudrate"].as<uint32_t>();
                if (loggingObj.containsKey("log_uart_traffic")) config.logging.log_uart_traffic = loggingObj["log_uart_traffic"].as<bool>();
                if (loggingObj.containsKey("log_can_traffic")) config.logging.log_can_traffic = loggingObj["log_can_traffic"].as<bool>();
                if (loggingObj.containsKey("log_cvl_changes")) config.logging.log_cvl_changes = loggingObj["log_cvl_changes"].as<bool>();
                if (loggingObj.containsKey("log_level")) {
                    if (loggingObj["log_level"].is<const char*>()) {
                        const char* level_cstr = loggingObj["log_level"].as<const char*>();
                        if (level_cstr != nullptr) {
                            String lvl = level_cstr;
                            lvl.toUpperCase();
                            if (lvl == "ERROR") config.logging.log_level = LOG_ERROR;
                            else if (lvl == "WARNING" || lvl == "WARN") config.logging.log_level = LOG_WARNING;
                            else if (lvl == "DEBUG") config.logging.log_level = LOG_DEBUG;
                            else config.logging.log_level = LOG_INFO;
                        }
                    } else {
                        config.logging.log_level = static_cast<LogLevel>(loggingObj["log_level"].as<int>());
                    }
                }
            }
        }

        // Advanced settings
        if (doc.containsKey("advanced")) {
            logger.log(LOG_DEBUG, "[API] Updating advanced config");
            JsonObjectConst advObj = doc["advanced"].as<JsonObjectConst>();
            if (!advObj.isNull()) {
                if (advObj.containsKey("enable_spiffs")) config.advanced.enable_spiffs = advObj["enable_spiffs"].as<bool>();
                if (advObj.containsKey("enable_ota")) config.advanced.enable_ota = advObj["enable_ota"].as<bool>();
                if (advObj.containsKey("watchdog_timeout_s")) config.advanced.watchdog_timeout_s = advObj["watchdog_timeout_s"].as<uint32_t>();
                if (advObj.containsKey("stack_size_bytes")) config.advanced.stack_size_bytes = advObj["stack_size_bytes"].as<uint32_t>();
            }
        }

        // Watchdog config
        if (doc.containsKey("watchdog_config")) {
            logger.log(LOG_DEBUG, "[API] Updating watchdog config");
            JsonObjectConst watchdogObj = doc["watchdog_config"].as<JsonObjectConst>();
            if (!watchdogObj.isNull()) {
                if (watchdogObj.containsKey("timeout_s")) config.advanced.watchdog_timeout_s = watchdogObj["timeout_s"].as<uint32_t>();
            }
        }

        if (configLockTaken) {
            xSemaphoreGive(configMutex);
            configLockTaken = false;
        }

        // Save configuration
        if (config.save()) {
            logger.log(LOG_INFO, "[API] System configuration saved successfully");
            request->send(200, "application/json", "{\"status\":\"Configuration updated\"}");
        } else {
            logger.log(LOG_ERROR, "[API] Failed to save system configuration");
            request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
        }
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