/**
 * @file system_init.cpp
 * @brief System initialization module with FreeRTOS + Logging
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "shared_data.h"
#include "logger.h"
#include "config_manager.h"
#include "tinybms_victron_bridge.h"
#include "tinybms_config_editor.h"
#include "event_bus.h"
#include "bridge_core.h"

// Watchdog integration
#include "watchdog_manager.h"
extern WatchdogManager Watchdog;

// External globals (from main.ino)
extern ConfigManager config;
extern TinyBMS_Victron_Bridge bridge;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t feedMutex;
extern Logger logger;
extern TinyBMSConfigEditor configEditor;
extern EventBus& eventBus;

extern TaskHandle_t webServerTaskHandle;
extern TaskHandle_t websocketTaskHandle;
extern TaskHandle_t watchdogTaskHandle;

// External functions
extern bool initWebServerTask();

namespace {

void feedWatchdogSafely() {
    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }
}

void publishStatusIfPossible(const char* message, StatusLevel level) {
    if (eventBus.isInitialized()) {
        eventBus.publishStatus(message, SOURCE_ID_SYSTEM, level);
    }
}

bool createTask(const char* name,
                TaskFunction_t task,
                uint32_t stack,
                void* params,
                UBaseType_t priority,
                TaskHandle_t* handle) {
    BaseType_t result = xTaskCreate(task, name, stack, params, priority, handle);
    if (result != pdPASS) {
        logger.log(LOG_ERROR, String("[TASK] Failed to create ") + name);
        return false;
    }

    logger.log(LOG_INFO, String("[TASK] ") + name + " created ✓");
    return true;
}

} // namespace

// ===================================================================================
// WiFi Initialization
// ===================================================================================
bool initializeWiFi() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   WiFi Configuration");
    logger.log(LOG_INFO, "========================================");

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "[WiFi] Failed to acquire config mutex");
        publishStatusIfPossible("WiFi configuration mutex unavailable", STATUS_LEVEL_ERROR);
        return false;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(config.wifi.hostname.c_str());
    logger.log(LOG_INFO, "[WiFi] Connecting to SSID: " + config.wifi.ssid);

    WiFi.begin(config.wifi.ssid.c_str(), config.wifi.password.c_str());

    uint8_t attempts = 0;
    const uint8_t MAX_ATTEMPTS = 20;

    while (WiFi.status() != WL_CONNECTED && attempts < MAX_ATTEMPTS) {
        feedWatchdogSafely();
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }

    bool success = false;

    if (WiFi.status() == WL_CONNECTED) {
        logger.log(LOG_INFO, "[WiFi] Connected ✓");
        logger.log(LOG_INFO, "[WiFi] IP Address: " + WiFi.localIP().toString());
        logger.log(LOG_INFO, "[WiFi] Hostname: " + config.wifi.hostname);
        logger.log(LOG_INFO, "[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
        publishStatusIfPossible("WiFi client connected", STATUS_LEVEL_NOTICE);
        success = true;
    } else if (config.wifi.ap_fallback.enabled) {
        logger.log(LOG_WARN, "[WiFi] Connection failed - starting AP mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(config.wifi.ap_fallback.ssid.c_str(), config.wifi.ap_fallback.password.c_str());
        logger.log(LOG_INFO, "[WiFi] AP Mode started ✓");
        logger.log(LOG_INFO, "[WiFi] AP SSID: " + config.wifi.ap_fallback.ssid);
        logger.log(LOG_INFO, "[WiFi] AP IP: " + WiFi.softAPIP().toString());
        publishStatusIfPossible("WiFi AP fallback active", STATUS_LEVEL_WARNING);
        success = true;
    } else {
        logger.log(LOG_ERROR, "[WiFi] Connection failed and AP fallback disabled");
        publishStatusIfPossible("WiFi unavailable (connection failed)", STATUS_LEVEL_ERROR);
    }

    xSemaphoreGive(configMutex);
    return success;
}

// ===================================================================================
// SPIFFS Initialization
// ===================================================================================
bool initializeSPIFFS() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   SPIFFS Filesystem");
    logger.log(LOG_INFO, "========================================");

    logger.log(LOG_INFO, "[SPIFFS] Mounting filesystem...");

    feedWatchdogSafely();

    if (!SPIFFS.begin(true)) {
        logger.log(LOG_ERROR, "[SPIFFS] Mount failed! Attempting format...");

        feedWatchdogSafely();

        if (!SPIFFS.format()) {
            logger.log(LOG_ERROR, "[SPIFFS] Format failed! Continuing without filesystem...");
            publishStatusIfPossible("SPIFFS unavailable", STATUS_LEVEL_ERROR);
            return false;
        }

        logger.log(LOG_INFO, "[SPIFFS] Formatted successfully");

        if (!SPIFFS.begin()) {
            logger.log(LOG_ERROR, "[SPIFFS] Mount failed even after format!");
            publishStatusIfPossible("SPIFFS mount failed after format", STATUS_LEVEL_ERROR);
            return false;
        }
    }

    logger.log(LOG_INFO, "[SPIFFS] Mounted successfully");
    logger.log(LOG_DEBUG, "[SPIFFS] Total space: " + String(SPIFFS.totalBytes()) + " bytes");
    logger.log(LOG_DEBUG, "[SPIFFS] Used space: " + String(SPIFFS.usedBytes()) + " bytes");

    File root = SPIFFS.open("/");
    File file = root.openNextFile();

    uint32_t total_size = 0;
    uint8_t file_count = 0;

    while (file) {
        logger.log(LOG_DEBUG, String("  - ") + file.name() + " (" + String(file.size()) + " bytes)");
        total_size += file.size();
        file_count++;
        file = root.openNextFile();
    }

    logger.log(LOG_DEBUG, "[SPIFFS] " + String(file_count) + " files, total " + String(total_size) + " bytes");
    publishStatusIfPossible("SPIFFS mounted", STATUS_LEVEL_NOTICE);
    return true;
}

// ===================================================================================
// Bridge Initialization
// ===================================================================================
bool initializeBridge() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   Bridge Initialization");
    logger.log(LOG_INFO, "========================================");

    feedWatchdogSafely();

    bool success = bridge.begin();

    if (!success) {
        logger.log(LOG_ERROR, "[BRIDGE] Initialization failed!");
        logger.log(LOG_WARN, "[BRIDGE] Continuing without bridge (web interface still available)");
        publishStatusIfPossible("Bridge unavailable", STATUS_LEVEL_ERROR);

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger.log(LOG_DEBUG, "  UART RX: GPIO" + String(config.hardware.uart.rx_pin));
            logger.log(LOG_DEBUG, "  UART TX: GPIO" + String(config.hardware.uart.tx_pin));
            logger.log(LOG_DEBUG, "  CAN TX: GPIO" + String(config.hardware.can.tx_pin));
            logger.log(LOG_DEBUG, "  CAN RX: GPIO" + String(config.hardware.can.rx_pin));
            xSemaphoreGive(configMutex);
        }
    } else {
        logger.log(LOG_INFO, "[BRIDGE] Initialized successfully ✓");
        publishStatusIfPossible("Bridge ready", STATUS_LEVEL_NOTICE);

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger.log(LOG_DEBUG, "[CONFIG] Bridge configuration:");
            logger.log(LOG_DEBUG, "  UART RX: GPIO" + String(config.hardware.uart.rx_pin));
            logger.log(LOG_DEBUG, "  UART TX: GPIO" + String(config.hardware.uart.tx_pin));
            logger.log(LOG_DEBUG, "  UART Baudrate: " + String(config.hardware.uart.baudrate));
            logger.log(LOG_DEBUG, "  CAN TX: GPIO" + String(config.hardware.can.tx_pin));
            logger.log(LOG_DEBUG, "  CAN RX: GPIO" + String(config.hardware.can.rx_pin));
            logger.log(LOG_DEBUG, "  CAN Bitrate: " + String(config.hardware.can.bitrate));
            logger.log(LOG_DEBUG, "  CVL Algorithm: " + String(config.cvl.enabled ? "Enabled" : "Disabled"));
            xSemaphoreGive(configMutex);
        }
    }

    return success;
}

// ===================================================================================
// Config Editor Initialization (placeholder for future implementation)
// ===================================================================================
bool initializeConfigEditor() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   TinyBMS Config Editor");
    logger.log(LOG_INFO, "========================================");

    feedWatchdogSafely();

    configEditor.begin();
    logger.log(LOG_INFO, "[CONFIG_EDITOR] Register catalog ready");
    publishStatusIfPossible("Config editor ready", STATUS_LEVEL_NOTICE);
    return true;
}

// ===================================================================================
// Global System Initialization
// ===================================================================================
bool initializeSystem() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   System Initialization");
    logger.log(LOG_INFO, "========================================");

    bool overall_ok = true;

    const bool spiffs_ok = initializeSPIFFS();
    overall_ok &= spiffs_ok;

    const bool event_bus_ok = eventBus.begin(EVENT_BUS_QUEUE_SIZE);
    if (!event_bus_ok) {
        logger.log(LOG_ERROR, "[EVENT_BUS] Initialization failed");
        overall_ok = false;
    } else {
        logger.log(LOG_INFO, "[EVENT_BUS] Initialized successfully ✓");
        publishStatusIfPossible("Event bus ready", STATUS_LEVEL_NOTICE);
        publishStatusIfPossible(spiffs_ok ? "SPIFFS mounted" : "SPIFFS unavailable",
                                spiffs_ok ? STATUS_LEVEL_NOTICE : STATUS_LEVEL_ERROR);
    }

    const bool wifi_ok = initializeWiFi();
    overall_ok &= wifi_ok;

    const bool bridge_ok = initializeBridge();
    overall_ok &= bridge_ok;

    bool bridge_tasks_ok = false;
    if (bridge_ok) {
        bridge_tasks_ok = Bridge_CreateTasks(&bridge);
        if (!bridge_tasks_ok) {
            logger.log(LOG_ERROR, "[BRIDGE] Task creation failed");
            overall_ok = false;
            publishStatusIfPossible("Bridge tasks unavailable", STATUS_LEVEL_ERROR);
        } else {
            publishStatusIfPossible("Bridge tasks running", STATUS_LEVEL_NOTICE);
        }
    }

    const bool config_editor_ok = initializeConfigEditor();
    overall_ok &= config_editor_ok;

    const bool web_task_ok = initWebServerTask();
    overall_ok &= web_task_ok;

    bool websocket_task_ok = createTask(
        "WebSocket",
        websocketTask,
        TASK_DEFAULT_STACK_SIZE,
        nullptr,
        TASK_NORMAL_PRIORITY,
        &websocketTaskHandle
    );
    overall_ok &= websocket_task_ok;

    bool watchdog_task_ok = createTask(
        "Watchdog",
        WatchdogManager::watchdogTask,
        2048,
        &Watchdog,
        TASK_NORMAL_PRIORITY,
        &watchdogTaskHandle
    );
    overall_ok &= watchdog_task_ok;

    feedWatchdogSafely();

    if (event_bus_ok) {
        publishStatusIfPossible(
            web_task_ok ? "Web server task running" : "Web server task failed",
            web_task_ok ? STATUS_LEVEL_NOTICE : STATUS_LEVEL_ERROR
        );
        publishStatusIfPossible(
            websocket_task_ok ? "WebSocket task running" : "WebSocket task failed",
            websocket_task_ok ? STATUS_LEVEL_NOTICE : STATUS_LEVEL_ERROR
        );
        publishStatusIfPossible(
            watchdog_task_ok ? "Watchdog task running" : "Watchdog task failed",
            watchdog_task_ok ? STATUS_LEVEL_NOTICE : STATUS_LEVEL_ERROR
        );
    }

    if (overall_ok) {
        logger.log(LOG_INFO, "[INIT] All subsystems initialized successfully ✓");
        publishStatusIfPossible("System initialization complete", STATUS_LEVEL_NOTICE);
    } else {
        logger.log(LOG_ERROR, "[INIT] One or more subsystems failed to initialize");
        publishStatusIfPossible("System initialization incomplete", STATUS_LEVEL_ERROR);
    }

    return overall_ok;
}
