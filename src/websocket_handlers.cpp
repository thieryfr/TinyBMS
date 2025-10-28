/**
 * @file websocket_handlers.cpp
 * @brief WebSocket handlers for TinyBMS-Victron Bridge with FreeRTOS + Logging
 */

#include <Arduino.h>
#include <algorithm>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "websocket_handlers.h"
#include "rtos_tasks.h"
#include "shared_data.h"
#include "json_builders.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "logger.h"        // ✅ Logging support
#include "config_manager.h"
#include "event_bus.h"     // Phase 3: Event Bus integration
#include "tiny_read_mapping.h"

extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern Logger logger;
extern TinyBMS_Victron_Bridge bridge;
extern EventBus& eventBus;  // Phase 3: Event Bus instance

// ====================================================================================
// WebSocket Event Handler
// ====================================================================================
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            logger.log(LOG_INFO, "WebSocket client #" + String(client->id()) + " connected");
            break;
        case WS_EVT_DISCONNECT:
            logger.log(LOG_INFO, "WebSocket client #" + String(client->id()) + " disconnected");
            break;
        case WS_EVT_DATA:
            logger.log(LOG_DEBUG, "WebSocket data received from client #" + String(client->id()));
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// ====================================================================================
// Build Status JSON
// ====================================================================================
void buildStatusJSON(String& output, const TinyBMS_LiveData& data) {
    StaticJsonDocument<1536> doc;

    doc["voltage"] = round(data.voltage * 100) / 100.0;
    doc["current"] = round(data.current * 10) / 10.0;
    doc["soc_percent"] = round(data.soc_percent * 10) / 10.0;
    doc["soh_percent"] = round(data.soh_percent * 10) / 10.0;
    doc["temperature"] = data.temperature;
    doc["min_cell_mv"] = data.min_cell_mv;
    doc["max_cell_mv"] = data.max_cell_mv;
    doc["cell_imbalance_mv"] = data.cell_imbalance_mv;
    doc["online_status"] = data.online_status;
    doc["uptime_ms"] = xTaskGetTickCount() * portTICK_PERIOD_MS;

    JsonArray registers = doc.createNestedArray("registers");
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

    serializeJson(doc, output);
}

// ====================================================================================
// Notify All Clients
// ====================================================================================
void notifyClients(const String& json) {
    ws.textAll(json);
}

// ====================================================================================
// WebSocket Task
// ====================================================================================
void websocketTask(void *pvParameters) {
    logger.log(LOG_INFO, "WebSocket task started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        static uint32_t last_update_ms = 0;

        ConfigManager::WebServerConfig web_config{};
        ConfigManager::LoggingConfig logging_config{};
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            web_config = config.web_server;
            logging_config = config.logging;
            xSemaphoreGive(configMutex);
        }

        const uint32_t interval_ms = std::max<uint32_t>(50, web_config.websocket_update_interval_ms);

        if (now - last_update_ms >= interval_ms) {

            TinyBMS_LiveData data;
            // Phase 3: Use Event Bus cache instead of legacy queue
            if (eventBus.getLatestLiveData(data)) {

                String json;
                buildStatusJSON(json, data);

                if (!json.isEmpty()) {
                    notifyClients(json);
                }

                if (logging_config.log_can_traffic) {
                    logger.log(LOG_DEBUG,
                        "WebSocket TX: V=" + String(data.voltage) +
                        " I=" + String(data.current) +
                        " SOC=" + String(data.soc_percent) + "%"
                    );
                }
            }

            last_update_ms = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        logger.log(LOG_DEBUG, "websocketTask stack: " + String(stackHighWaterMark));

        vTaskDelay(pdMS_TO_TICKS(interval_ms));
    }
}
