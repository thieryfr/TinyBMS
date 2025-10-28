/**
 * @file websocket_handlers.cpp
 * @brief WebSocket handlers for TinyBMS-Victron Bridge with FreeRTOS + Logging
 */

#include <Arduino.h>
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
    StaticJsonDocument<640> doc;

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

        if (now - last_update_ms >= config.web_server.websocket_update_interval_ms) {

            TinyBMS_LiveData data;
            // Phase 3: Use Event Bus cache instead of legacy queue
            if (eventBus.getLatestLiveData(data)) {

                String json;
                if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    buildStatusJSON(json, data);
                    xSemaphoreGive(configMutex);
                }

                notifyClients(json);

                if (config.logging.log_can_traffic) {
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

        vTaskDelay(pdMS_TO_TICKS(config.web_server.websocket_update_interval_ms));
    }
}