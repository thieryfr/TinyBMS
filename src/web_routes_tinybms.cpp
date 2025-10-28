/**
 * @file web_routes_tinybms.cpp
 * @brief API routes for TinyBMS configuration with Logging support
 * @version 1.1 - Logging + Mutex Safety
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Freertos.h>
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "logger.h"         // ✅ Logging
#include "config_manager.h" // For config.logging.log_level awareness

// External globals
extern TinyBMS_Victron_Bridge bridge;
extern ConfigManager config;
extern TinyBMSConfigEditor configEditor;
extern SemaphoreHandle_t configMutex;
extern Logger logger;       // ✅ Logger instance

// External functions
extern String getConfigJSON();

/**
 * @brief Register TinyBMS API routes
 */
void registerTinyBMSRoutes(AsyncWebServer& server) {

    logger.log(LOG_INFO, "[API] Registering TinyBMS config routes");

    // ============================
    // GET /api/config/tinybms
    // ============================
    server.on("/api/config/tinybms", HTTP_GET, [](AsyncWebServerRequest *request) {

        if (config.logging.log_level >= LOG_INFO) {
            logger.log(LOG_INFO, "[API] GET /api/config/tinybms");
        }

        request->send(200, "application/json", getConfigJSON());
    });

    // ============================
    // PUT /api/config/tinybms
    // ============================
    server.on("/api/config/tinybms", HTTP_PUT, [](AsyncWebServerRequest *request) {

        logger.log(LOG_INFO, "[API] PUT /api/config/tinybms");

        if (!request->hasArg("plain")) {
            logger.log(LOG_WARN, "[API] Missing JSON body");
            request->send(400, "application/json", "{\"error\":\"Missing body\"}");
            return;
        }

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, request->arg("plain"));

        if (error) {
            logger.log(LOG_ERROR, String("[API] JSON parse error: ") + error.c_str());
            request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
            return;
        }

        TinyBMS_Config cfg = bridge.getConfig();

        // ✅ Update only changed fields
        if (doc.containsKey("fully_charged_voltage_mv"))
            cfg.fully_charged_voltage_mv = doc["fully_charged_voltage_mv"];
        if (doc.containsKey("fully_discharged_voltage_mv"))
            cfg.fully_discharged_voltage_mv = doc["fully_discharged_voltage_mv"];
        if (doc.containsKey("charge_finished_current_ma"))
            cfg.charge_finished_current_ma = doc["charge_finished_current_ma"];
        if (doc.containsKey("battery_capacity_ah"))
            cfg.battery_capacity_ah_scaled = doc["battery_capacity_ah"].as<float>() * 100;
        if (doc.containsKey("cell_count"))
            cfg.cell_count = doc["cell_count"];
        if (doc.containsKey("overvoltage_cutoff_mv"))
            cfg.overvoltage_cutoff_mv = doc["overvoltage_cutoff_mv"];
        if (doc.containsKey("undervoltage_cutoff_mv"))
            cfg.undervoltage_cutoff_mv = doc["undervoltage_cutoff_mv"];
        if (doc.containsKey("discharge_overcurrent_a"))
            cfg.discharge_overcurrent_a = doc["discharge_overcurrent_a"];
        if (doc.containsKey("charge_overcurrent_a"))
            cfg.charge_overcurrent_a = doc["charge_overcurrent_a"];
        if (doc.containsKey("overheat_temp_c"))
            cfg.overheat_cutoff_c = doc["overheat_temp_c"].as<float>() * 10;
        if (doc.containsKey("low_temp_charge_c"))
            cfg.low_temp_charge_cutoff_c = doc["low_temp_charge_c"].as<float>() * 10;

        // ✅ Mutex protection
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            logger.log(LOG_ERROR, "[API] Failed to acquire config mutex");
            request->send(500, "application/json", "{\"error\":\"Failed to access config\"}");
            return;
        }

        // ✅ Commit change
        if (configEditor.writeConfig(cfg)) {
            logger.log(LOG_INFO, "[API] TinyBMS configuration updated");
            request->send(200, "application/json", "{\"status\":\"Configuration updated\"}");
        } else {
            logger.log(LOG_ERROR, "[API] Failed to update TinyBMS configuration");
            request->send(500, "application/json", "{\"error\":\"Failed to update configuration\"}");
        }

        xSemaphoreGive(configMutex);
    });
}