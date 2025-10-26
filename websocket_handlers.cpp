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

extern AsyncWebServer server;
extern QueueHandle_t liveDataQueue;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern ConfigManager config;
extern WatchdogManager Watchdog;
extern Logger logger;      // ✅ Logger instance

void websocketTask(void *pvParameters) {
    logger.log(LOG_INFO, "WebSocket task started");   // ✅ Task start banner for diagnostics

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        static uint32_t last_update_ms = 0;
        
        if (now - last_update_ms >= config.web_server.websocket_update_interval_ms) {

            TinyBMS_LiveData data;
            if (xQueuePeek(liveDataQueue, &data, 0) == pdTRUE) {

                String json;
                if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    buildStatusJSON(json, data);
                    xSemaphoreGive(configMutex);
                }

                notifyClients(json);

                // ✅ Optional logging (quiet by default)
                if (config.logging.log_can_traffic) {
                    logger.log(LOG_DEBUG,
                        "WebSocket TX: V=" + String(data.voltage) +
                        " I=" + String(data.current) +
                        " SOC=" + String(data.soc_percent) + "%"
                    );
                }
            }

            last_update_ms = now;

            // ✅ Feed watchdog safely
            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }
        
        // ✅ Stack monitoring (Debug only, not spam)
        UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        logger.log(LOG_DEBUG, "websocketTask stack: " + String(stackHighWaterMark));
        
        vTaskDelay(pdMS_TO_TICKS(config.web_server.websocket_update_interval_ms));
    }
}