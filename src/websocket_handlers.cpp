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
#include "event/event_bus_v2.h"     // Phase 3: Event Bus integration
#include "event/event_types_v2.h"
#include "tiny_read_mapping.h"
#include "optimization/websocket_throttle.h"

extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern Logger logger;
extern TinyBMS_Victron_Bridge bridge;
extern SemaphoreHandle_t statsMutex;
using tinybms::event::eventBus;  // Phase 3: Event Bus instance
using tinybms::events::EventSource;
using tinybms::events::LiveDataUpdate;
using tinybms::events::StatusMessage;

namespace {
optimization::WebsocketThrottle ws_throttle;
optimization::WebsocketThrottleConfig active_ws_config{};
bool ws_throttle_configured = false;
}

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

    StatusMessage status_event{};
    if (eventBus.getLatest(status_event)) {
        JsonObject status = doc.createNestedObject("status_message");
        status["message"] = status_event.message;
        status["level"] = static_cast<uint8_t>(status_event.level);
        static const char* level_names[] = {"info", "notice", "warning", "error"};
        auto level_index = static_cast<size_t>(status_event.level);
        if (level_index < (sizeof(level_names) / sizeof(level_names[0]))) {
            status["level_name"] = level_names[level_index];
        }
        status["source_id"] = static_cast<uint32_t>(status_event.metadata.source);
        status["timestamp_ms"] = status_event.metadata.timestamp_ms;
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
        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            web_config = config.web_server;
            logging_config = config.logging;
            xSemaphoreGive(configMutex);
        }

        optimization::WebsocketThrottleConfig throttle_config{};
        throttle_config.min_interval_ms = std::max<uint32_t>(100, web_config.websocket_min_interval_ms);
        throttle_config.burst_window_ms = std::max<uint32_t>(throttle_config.min_interval_ms, web_config.websocket_burst_window_ms);
        throttle_config.max_burst_count = std::max<uint32_t>(1, web_config.websocket_burst_max);
        throttle_config.max_payload_bytes = web_config.websocket_max_payload_bytes;

        if (!ws_throttle_configured ||
            throttle_config.min_interval_ms != active_ws_config.min_interval_ms ||
            throttle_config.burst_window_ms != active_ws_config.burst_window_ms ||
            throttle_config.max_burst_count != active_ws_config.max_burst_count ||
            throttle_config.max_payload_bytes != active_ws_config.max_payload_bytes) {
            ws_throttle.configure(throttle_config);
            active_ws_config = throttle_config;
            ws_throttle_configured = true;
            logger.log(LOG_INFO,
                String("WebSocket throttle updated: min=") + throttle_config.min_interval_ms +
                "ms window=" + throttle_config.burst_window_ms +
                "ms burst=" + throttle_config.max_burst_count +
                " payload<=" + throttle_config.max_payload_bytes + "B");
        }

        const uint32_t interval_ms = std::max<uint32_t>(throttle_config.min_interval_ms,
                                                         std::max<uint32_t>(100, web_config.websocket_update_interval_ms));

        if (now - last_update_ms >= interval_ms) {

            LiveDataUpdate latest{};
            // Phase 3: Use Event Bus cache instead of legacy queue
            if (eventBus.getLatest(latest)) {
                TinyBMS_LiveData data = latest.data;

                String json;
                buildStatusJSON(json, data);

                if (!json.isEmpty()) {
                    const size_t payload_size = static_cast<size_t>(json.length());
                    if (ws_throttle.shouldSend(now, payload_size)) {
                        notifyClients(json);
                        ws_throttle.recordSend(now, payload_size);
                        if (xSemaphoreTake(statsMutex, portMAX_DELAY) == pdTRUE) {
                            bridge.stats.websocket_sent_count++;
                            xSemaphoreGive(statsMutex);
                        }

                        if (logging_config.log_can_traffic) {
                            logger.log(LOG_DEBUG,
                                "WebSocket TX: V=" + String(data.voltage) +
                                " I=" + String(data.current) +
                                " SOC=" + String(data.soc_percent) + "%"
                            );
                        }
                    } else {
                        ws_throttle.recordDrop();
                        if (xSemaphoreTake(statsMutex, portMAX_DELAY) == pdTRUE) {
                            bridge.stats.websocket_dropped_count++;
                            xSemaphoreGive(statsMutex);
                        }

                        if (logging_config.log_can_traffic) {
                            logger.log(LOG_DEBUG,
                                String("WebSocket throttled (min ") + active_ws_config.min_interval_ms +
                                "ms, burst " + active_ws_config.max_burst_count + ")");
                        }
                    }
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
