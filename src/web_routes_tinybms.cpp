/**
 * @file web_routes_tinybms.cpp
 * @brief API routes for TinyBMS configuration with Logging support
 * @version 1.2 - Phase 3: Dual WebServer Support (AsyncWebServer + ESP-IDF)
 */

#include <Arduino.h>

// Conditional WebServer includes
#ifdef USE_ESP_IDF_WEBSERVER
    #include "esp_http_server_wrapper.h"
    using tinybms::web::HttpServerIDF;
    using tinybms::web::HttpRequestIDF;
    using WebServerType = HttpServerIDF;
    using WebRequestType = HttpRequestIDF;
#else
    #include <ESPAsyncWebServer.h>
    using WebServerType = AsyncWebServer;
    using WebRequestType = AsyncWebServerRequest;
#endif

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

void sendJsonResponse(WebRequestType* request, int statusCode, JsonDocument& doc) {
    String payload;
    serializeJson(doc, payload);
    request->send(statusCode, "application/json", payload);
}

void sendErrorResponse(WebRequestType* request, int statusCode, const String& message, const char* code = nullptr) {
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
void setupTinyBMSConfigRoutes(WebServerType& server) {

    logger.log(LOG_INFO, "[API] Registering TinyBMS config routes");

    // ============================
    // GET /api/tinybms/registers
    // ============================
    server.on("/api/tinybms/registers", HTTP_GET, [](AsyncWebServerRequest *request) {
        logger.log(LOG_DEBUG, "[API] GET /api/tinybms/registers");
        request->send(200, "application/json", configEditor.getRegistersJSON());
    });

    // ============================
    // GET /api/tinybms/register
    // ============================
    server.on("/api/tinybms/register", HTTP_GET, [](AsyncWebServerRequest *request) {
        uint16_t address = 0;
        bool resolved = false;
        String keyParam;

        if (request->hasParam("key")) {
            keyParam = request->getParam("key")->value();
            const TinyRwRegisterMetadata* meta = findTinyRwRegisterByKey(keyParam);
            if (!meta) {
                sendErrorResponse(request, 404, "Unknown register key", "unknown_key");
                return;
            }
            address = meta->address;
            resolved = true;
        }

        if (!resolved) {
            if (!request->hasParam("address")) {
                sendErrorResponse(request, 400, "Missing address parameter", "missing_address");
                return;
            }
            address = request->getParam("address")->value().toInt();
        }

        float user_value = 0.0f;
        bool ok = configEditor.readRegister(address, user_value);
        const TinyBMSConfigRegister* reg = configEditor.getRegister(address);

        StaticJsonDocument<512> doc;
        doc["success"] = ok;
        doc["address"] = address;
        if (reg) {
            doc["key"] = reg->key;
            doc["unit"] = reg->unit;
            doc["precision"] = reg->precision;
            doc["raw_value"] = reg->current_raw_value;
        }

        if (ok && reg) {
            doc["value"] = reg->current_user_value;
        } else if (!ok) {
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

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }

        uint16_t address = 0;
        bool resolved = false;
        if (doc.containsKey("key")) {
            String key = doc["key"].as<String>();
            const TinyRwRegisterMetadata* meta = findTinyRwRegisterByKey(key);
            if (!meta) {
                sendErrorResponse(request, 404, "Unknown register key", "unknown_key");
                return;
            }
            address = meta->address;
            resolved = true;
        }

        if (!resolved) {
            if (!doc.containsKey("address")) {
                sendErrorResponse(request, 400, "Missing address or key", "missing_fields");
                return;
            }
            address = doc["address"].as<uint16_t>();
        }

        if (!doc.containsKey("value")) {
            sendErrorResponse(request, 400, "Missing value", "missing_fields");
            return;
        }

        float user_value = doc["value"].as<float>();
        TinyBMSConfigError err = configEditor.writeRegister(address, user_value);
        const TinyBMSConfigRegister* reg = configEditor.getRegister(address);

        StaticJsonDocument<512> resp;
        bool ok = err == TinyBMSConfigError::None;
        resp["success"] = ok;
        resp["address"] = address;
        if (reg) {
            resp["key"] = reg->key;
        }

        if (ok && reg) {
            resp["value"] = reg->current_user_value;
            resp["raw_value"] = reg->current_raw_value;
        } else if (!ok) {
            resp["message"] = tinybmsConfigErrorToString(err);
        }

        sendJsonResponse(request, ok ? 200 : 500, resp);
    });

    // ============================
    // POST /api/tinybms/registers/read-all
    // ============================
    server.on("/api/tinybms/registers/read-all", HTTP_POST, [](AsyncWebServerRequest *request) {
        logger.log(LOG_INFO, "[API] POST /api/tinybms/registers/read-all");
        uint8_t success_count = configEditor.readAllRegisters();

        DynamicJsonDocument doc(16384);
        DeserializationError err = deserializeJson(doc, configEditor.getRegistersJSON());
        if (err) {
            sendErrorResponse(request, 500, "Failed to build register list", "serialization_error");
            return;
        }

        doc["read_count"] = success_count;
        sendJsonResponse(request, 200, doc);
    });

    // ============================
    // POST /api/tinybms/registers/batch
    // ============================
    server.on("/api/tinybms/registers/batch", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (!request->hasArg("plain")) {
            sendErrorResponse(request, 400, "Missing body", "missing_body");
            return;
        }

        StaticJsonDocument<2048> body;
        if (deserializeJson(body, request->arg("plain"))) {
            sendErrorResponse(request, 400, "Invalid JSON", "invalid_json");
            return;
        }

        JsonArray regs = body["registers"].as<JsonArray>();
        if (regs.isNull()) {
            sendErrorResponse(request, 400, "Missing registers array", "missing_fields");
            return;
        }

        DynamicJsonDocument resp(4096);
        JsonArray results = resp.createNestedArray("results");
        size_t success = 0;
        size_t failure = 0;

        for (JsonVariant entry : regs) {
            JsonObject obj = entry.as<JsonObject>();
            JsonObject result = results.createNestedObject();

            uint16_t address = 0;
            bool resolved = false;
            if (obj.containsKey("key")) {
                String key = obj["key"].as<String>();
                const TinyRwRegisterMetadata* meta = findTinyRwRegisterByKey(key);
                if (meta) {
                    address = meta->address;
                    resolved = true;
                } else {
                    result["success"] = false;
                    result["error"] = "unknown_key";
                    result["key"] = key;
                    failure++;
                    continue;
                }
            }

            if (!resolved) {
                if (!obj.containsKey("address")) {
                    result["success"] = false;
                    result["error"] = "missing_address";
                    failure++;
                    continue;
                }
                address = obj["address"].as<uint16_t>();
            }

            if (!obj.containsKey("value")) {
                result["success"] = false;
                result["address"] = address;
                result["error"] = "missing_value";
                failure++;
                continue;
            }

            float value = obj["value"].as<float>();
            TinyBMSConfigError err = configEditor.writeRegister(address, value);
            const TinyBMSConfigRegister* reg = configEditor.getRegister(address);

            result["address"] = address;
            if (reg) {
                result["key"] = reg->key;
            }

            if (err == TinyBMSConfigError::None && reg) {
                result["success"] = true;
                result["value"] = reg->current_user_value;
                result["raw_value"] = reg->current_raw_value;
                success++;
            } else if (err == TinyBMSConfigError::None) {
                result["success"] = true;
                success++;
            } else {
                result["success"] = false;
                result["error"] = tinybmsConfigErrorToString(err);
                failure++;
            }
        }

        bool overall = failure == 0;
        resp["success"] = overall;
        resp["written"] = success;
        resp["failed"] = failure;

        int status = overall ? 200 : (success > 0 ? 207 : 500);
        sendJsonResponse(request, status, resp);
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
            logger.log(LOG_WARN, "[API] Missing JSON body");
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
            cfg.battery_capacity_ah = doc["battery_capacity_ah"].as<float>();
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
            cfg.overheat_cutoff_c = doc["overheat_temp_c"].as<float>();
        if (doc.containsKey("low_temp_charge_c"))
            cfg.low_temp_charge_cutoff_c = doc["low_temp_charge_c"].as<float>();

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
