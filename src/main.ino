/**
 * @file main.ino
 * @brief TinyBMS to Victron CAN-BMS Bridge with FreeRTOS + Logging System
 */

#include <Arduino.h>
#include "rtos_tasks.h"
#include "system_init.h"
#include "shared_data.h"
#include "watchdog_manager.h"
#include "tinybms_victron_bridge.h"
#include "rtos_config.h"
#include "config_manager.h"
#include "logger.h"
#include "event_bus.h"
#include "tinybms_config_editor.h"

// Global resources
SemaphoreHandle_t uartMutex;
SemaphoreHandle_t feedMutex;
SemaphoreHandle_t configMutex;

// Web Server objects
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

ConfigManager config;
WatchdogManager Watchdog;
TinyBMS_Victron_Bridge bridge;
Logger logger;
TinyBMSConfigEditor configEditor;

// Task handles
TaskHandle_t webServerTaskHandle = NULL;
TaskHandle_t websocketTaskHandle = NULL;
TaskHandle_t watchdogTaskHandle = NULL;

void webServerTask(void *pvParameters) {
    setupWebServer();
    logger.log(LOG_INFO, "Web server started");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));

        // Stack monitoring → Debug only
        UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        logger.log(LOG_DEBUG, "webServerTask stack: " + String(stackHighWaterMark));

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void setup() {
    Serial.begin(115200);

    // Create mutexes early for configuration and logging dependencies
    uartMutex = xSemaphoreCreateMutex();
    feedMutex = xSemaphoreCreateMutex();
    configMutex = xSemaphoreCreateMutex();

    if (!uartMutex || !feedMutex || !configMutex) {
        Serial.println("[INIT] ❌ Mutex creation failed");
        while (true) {
            delay(1000);
        }
    }

    Serial.println("[INIT] Mutexes created");

    // Load configuration (uses configMutex internally)
    if (!config.begin()) {
        Serial.println("[CONFIG] ⚠ Using default configuration");
    } else {
        Serial.println("[CONFIG] Configuration loaded");
    }

    const uint32_t desired_baud = config.logging.serial_baudrate > 0 ?
        config.logging.serial_baudrate : 115200;
    if (desired_baud != 115200) {
        Serial.begin(desired_baud);
    }

    // Initialize Logger (after configuration)
    if (!logger.begin(config)) {
        Serial.println("[LOGGER] ⚠ Failed to initialize logging to SPIFFS, Serial logging only");
    } else {
        logger.log(LOG_INFO, "Logging system initialized");
    }

    // Initialize watchdog using configured timeout
    if (!Watchdog.begin(config.advanced.watchdog_timeout_s * 1000)) {
        logger.log(LOG_ERROR, "Watchdog initialization failed");
    } else {
        logger.log(LOG_INFO, "Watchdog started");
    }

    // Initialize remaining subsystems
    if (!initializeSystem()) {
        logger.log(LOG_ERROR, "System initialization completed with errors");
    }
}

void loop() {
    vTaskDelay(portMAX_DELAY); // ✅ FreeRTOS runs everything
}
