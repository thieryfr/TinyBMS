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
void registerApiRoutes(AsyncWebServer& server) {

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

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            logger.log(LOG_ERROR, "[API] Failed to acquire config mutex");
            request->send(500, "application/json", "{\"error\":\"Failed to access config\"}");
            return;
        }

        // WiFi settings
        if (doc.containsKey("wifi")) {
            logger.log(LOG_DEBUG, "[API] Updating WiFi config");
            if (doc["wifi"].containsKey("ssid")) config.wifi.ssid = doc["wifi"]["ssid"].as<String>();
            if (doc["wifi"].containsKey("password")) config.wifi.password = doc["wifi"]["password"].as<String>();
            if (doc["wifi"].containsKey("hostname")) config.wifi.hostname = doc["wifi"]["hostname"].as<String>();
            if (doc["wifi"].containsKey("ap_fallback_enabled")) config.wifi.ap_fallback_enabled = doc["wifi"]["ap_fallback_enabled"].as<bool>();
        }

        // Hardware settings
        if (doc.containsKey("hardware")) {
            logger.log(LOG_DEBUG, "[API] Updating hardware config");
            if (doc["hardware"].containsKey("uart")) {
                config.hardware.uart.rx_pin = doc["hardware"]["uart"]["rx_pin"] | config.hardware.uart.rx_pin;
                config.hardware.uart.tx_pin = doc["hardware"]["uart"]["tx_pin"] | config.hardware.uart.tx_pin;
                config.hardware.uart.baudrate = doc["hardware"]["uart"]["baudrate"] | config.hardware.uart.baudrate;
            }
            if (doc["hardware"].containsKey("can")) {
                config.hardware.can.rx_pin = doc["hardware"]["can"]["rx_pin"] | config.hardware.can.rx_pin;
                config.hardware.can.tx_pin = doc["hardware"]["can"]["tx_pin"] | config.hardware.can.tx_pin;
                config.hardware.can.bitrate = doc["hardware"]["can"]["bitrate"] | config.hardware.can.bitrate;
            }
        }

        // CVL Algorithm
        if (doc.containsKey("cvl_algorithm")) {
            logger.log(LOG_DEBUG, "[API] Updating CVL algorithm config");
            if (doc["cvl_algorithm"].containsKey("enabled")) config.cvl.enabled = doc["cvl_algorithm"]["enabled"].as<bool>();
            if (doc["cvl_algorithm"].containsKey("bulk_threshold")) config.cvl.bulk_soc_threshold = doc["cvl_algorithm"]["bulk_threshold"].as<float>();
            if (doc["cvl_algorithm"].containsKey("float_threshold")) config.cvl.float_soc_threshold = doc["cvl_algorithm"]["float_threshold"].as<float>();
            if (doc["cvl_algorithm"].containsKey("float_exit")) config.cvl.float_exit_soc = doc["cvl_algorithm"]["float_exit"].as<float>();
        }

        // Victron settings
        if (doc.containsKey("victron")) {
            logger.log(LOG_DEBUG, "[API] Updating Victron config");
            if (doc["victron"].containsKey("manufacturer")) config.victron.manufacturer_name = doc["victron"]["manufacturer"].as<String>();
            if (doc["victron"].containsKey("battery_name")) config.victron.battery_name = doc["victron"]["battery_name"].as<String>();
            if (doc["victron"].containsKey("pgn_update_ms")) config.victron.pgn_update_interval_ms = doc["victron"]["pgn_update_ms"] | config.victron.pgn_update_interval_ms;
            if (doc["victron"].containsKey("cvl_update_ms")) config.victron.cvl_update_interval_ms = doc["victron"]["cvl_update_ms"] | config.victron.cvl_update_interval_ms;
        }

        // Watchdog config
        if (doc.containsKey("watchdog_config")) {
            logger.log(LOG_DEBUG, "[API] Updating watchdog config");
            if (doc["watchdog_config"].containsKey("timeout_s")) config.advanced.watchdog_timeout_s = doc["watchdog_config"]["timeout_s"] | config.advanced.watchdog_timeout_s;
        }

        // Save configuration
        if (config.save("/config.json")) {
            logger.log(LOG_INFO, "[API] System configuration saved successfully");
            request->send(200, "application/json", "{\"status\":\"Configuration updated\"}");
        } else {
            logger.log(LOG_ERROR, "[API] Failed to save system configuration");
            request->send(500, "application/json", "{\"error\":\"Failed to save configuration\"}");
        }

        xSemaphoreGive(configMutex);
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
                config.save("/config.json");
                xSemaphoreGive(configMutex);
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