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
TaskHandle_t uartTaskHandle = NULL;
TaskHandle_t canTaskHandle = NULL;
TaskHandle_t cvlTaskHandle = NULL;

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
    // ✅ Baudrate configurable (fallback si erreur JSON ou invalide)
    Serial.begin(
        config.logging.serial_baudrate > 0 ?
        config.logging.serial_baudrate :
        115200
    );

    // Load config
    if (!config.begin()) {
        logger.log(LOG_WARNING, "Configuration load failed – defaults applied");
    } else {
        logger.log(LOG_INFO, "Configuration loaded");
    }

    // ✅ Initialize Logger (après config)
    if (!logger.begin(config)) {
        Serial.println("[LOGGER] ⚠ Failed to initialize logging to SPIFFS, Serial logging only");
    } else {
        logger.log(LOG_INFO, "Logging system initialized");
    }

    configEditor.begin();

    // Initialize watchdog
    Watchdog.begin(config.advanced.watchdog_timeout_s * 1000);
    logger.log(LOG_INFO, "Watchdog started");

    // Create mutexes
    uartMutex = xSemaphoreCreateMutex();
    feedMutex = xSemaphoreCreateMutex();
    configMutex = xSemaphoreCreateMutex();

    if (!uartMutex || !feedMutex || !configMutex) {
        logger.log(LOG_ERROR, "Mutex creation failed");
        while (true);
    }
    logger.log(LOG_INFO, "Mutexes created");

    // Phase 6: Event Bus fully replaces legacy queue
    if (!eventBus.begin(EVENT_BUS_QUEUE_SIZE)) {
        logger.log(LOG_ERROR, "Event Bus initialization failed");
    } else {
        logger.log(LOG_INFO, "Event Bus initialized (all components migrated)");
    }

    // Initialize bridge
    if (!bridge.begin()) {
        logger.log(LOG_ERROR, "Bridge initialization failed");
    } else {
        logger.log(LOG_INFO, "Bridge initialized");
    }

    // Create tasks
    xTaskCreate(webServerTask, "WebServer", TASK_DEFAULT_STACK_SIZE, NULL, TASK_NORMAL_PRIORITY, &webServerTaskHandle);
    xTaskCreate(websocketTask, "WebSocket", TASK_DEFAULT_STACK_SIZE, NULL, TASK_NORMAL_PRIORITY, &websocketTaskHandle);
    xTaskCreate(WatchdogManager::watchdogTask, "Watchdog", 2048, &Watchdog, TASK_NORMAL_PRIORITY, &watchdogTaskHandle);
    xTaskCreate(TinyBMS_Victron_Bridge::uartTask, "UART", TASK_DEFAULT_STACK_SIZE, &bridge, TASK_HIGH_PRIORITY, &uartTaskHandle);
    xTaskCreate(TinyBMS_Victron_Bridge::canTask, "CAN", TASK_DEFAULT_STACK_SIZE, &bridge, TASK_HIGH_PRIORITY, &canTaskHandle);
    xTaskCreate(TinyBMS_Victron_Bridge::cvlTask, "CVL", 2048, &bridge, TASK_NORMAL_PRIORITY, &cvlTaskHandle);

    logger.log(LOG_INFO, "All tasks started");
}

void loop() {
    vTaskDelay(portMAX_DELAY); // ✅ FreeRTOS runs everything
}