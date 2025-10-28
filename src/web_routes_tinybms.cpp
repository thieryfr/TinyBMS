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
#include "tinybms_config_editor.h"
#include "web_routes.h"

// External globals
extern TinyBMS_Victron_Bridge bridge;
extern ConfigManager config;
extern TinyBMSConfigEditor configEditor;
extern SemaphoreHandle_t configMutex;
extern Logger logger;       // ✅ Logger instance

// External functions
extern String getConfigJSON();

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

}

/**
 * @brief Register TinyBMS API routes
 */
void setupTinyBMSConfigRoutes(AsyncWebServer& server) {

    logger.log(LOG_INFO, "[API] Registering TinyBMS config routes");

    // ============================
    // GET /api/tinybms/register
    // ============================
    server.on("/api/tinybms/register", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("address")) {
            sendErrorResponse(request, 400, "Missing address parameter", "missing_address");
            return;
        }

        uint16_t address = request->getParam("address")->value().toInt();
        uint16_t value = 0;
        bool ok = configEditor.readRegister(address, value);

        StaticJsonDocument<256> doc;
        doc["success"] = ok;
        doc["address"] = address;
        if (ok) {
            doc["value"] = value;
        } else {
            doc["message"] = "Failed to read register";
        }
        sendJsonResponse(request, ok ? 200 : 500, doc);
    });

    // ============================
    // POST /api/tinybms/register
    // ============================
    server.on("/api/tinybms/register", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }

        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }

        if (!doc.containsKey("address") || !doc.containsKey("value")) {
            sendErrorResponse(request, 400, "Missing address or value", "missing_fields");
            return;
        }

        uint16_t address = doc["address"].as<uint16_t>();
        uint16_t value = doc["value"].as<uint16_t>();
        TinyBMSConfigError err = configEditor.writeRegister(address, value);

        StaticJsonDocument<256> resp;
        bool ok = err == TinyBMSConfigError::None;
        resp["success"] = ok;
        resp["address"] = address;
        if (!ok) {
            resp["message"] = tinybmsConfigErrorToString(err);
        }
        sendJsonResponse(request, ok ? 200 : 500, resp);
    });

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
            logger.log(LOG_WARNING, "[API] Missing JSON body");
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }

        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, request->arg("plain"));

        if (error) {
            logger.log(LOG_ERROR, String("[API] JSON parse error: ") + error.c_str());
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
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
            sendErrorResponse(request, 503, "Failed to access config", "config_mutex_timeout");
            return;
        }

        // ✅ Commit change
        TinyBMSConfigResult result = configEditor.writeConfig(cfg);
        if (result.ok()) {
            logger.log(LOG_INFO, "[API] TinyBMS configuration updated");
            StaticJsonDocument<128> resp;
            resp["success"] = true;
            resp["message"] = "Configuration updated";
            sendJsonResponse(request, 200, resp);
        } else {
            TinyBMSConfigError error = result.error;
            const char* code = tinybmsConfigErrorToString(error);

            int status = 500;
            switch (error) {
                case TinyBMSConfigError::BridgeUnavailable:
                case TinyBMSConfigError::MutexUnavailable:
                    status = 503;
                    break;
                case TinyBMSConfigError::RegisterNotFound:
                    status = 404;
                    break;
                case TinyBMSConfigError::OutOfRange:
                    status = 422;
                    break;
                case TinyBMSConfigError::Timeout:
                    status = 504;
                    break;
                case TinyBMSConfigError::WriteFailed:
                    status = 502;
                    break;
                default:
                    status = 500;
                    break;
            }

            logger.log(LOG_ERROR, String("[API] TinyBMS config update failed: ") + result.message);

            StaticJsonDocument<256> errorDoc;
            errorDoc["success"] = false;
            errorDoc["message"] = result.message;
            errorDoc["error"] = code;
            sendJsonResponse(request, status, errorDoc);
        }

        xSemaphoreGive(configMutex);
    });

    // ============================
    // POST /api/tinybms/factory-reset
    // ============================
    server.on("/api/tinybms/factory-reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<128> doc;
        doc["success"] = false;
        doc["message"] = "Factory reset not supported";
        String payload;
        serializeJson(doc, payload);
        request->send(501, "application/json", payload);
    });
}
