/**
 * @file system_init.cpp
 * @brief System initialization module with FreeRTOS + Logging
 */

#include <Arduino.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <Freertos.h>
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "shared_data.h"
#include "logger.h"  // ✅ Added
#include "config_manager.h"

// Watchdog integration
#include "watchdog_manager.h"
extern WatchdogManager Watchdog;

// External globals (from main.ino)
extern ConfigManager config;
extern TinyBMS_Victron_Bridge bridge;
extern TinyBMSConfigEditor configEditor;
extern SemaphoreHandle_t configMutex;
extern SemaphoreHandle_t feedMutex;
extern QueueHandle_t liveDataQueue;
extern Logger logger;

// External functions
extern void initWebServerTask();

// ===================================================================================
// WiFi Initialization
// ===================================================================================
void initializeWiFi() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   WiFi Configuration");
    logger.log(LOG_INFO, "========================================");

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "[WiFi] Failed to acquire config mutex");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(config.wifi.hostname.c_str());
    logger.log(LOG_INFO, "[WiFi] Connecting to SSID: " + config.wifi.ssid);

    WiFi.begin(config.wifi.ssid.c_str(), config.wifi.password.c_str());

    uint8_t attempts = 0;
    const uint8_t MAX_ATTEMPTS = 20;

    while (WiFi.status() != WL_CONNECTED && attempts < MAX_ATTEMPTS) {
        if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Watchdog.feed();
            xSemaphoreGive(feedMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        logger.log(LOG_INFO, "[WiFi] Connected ✓");
        logger.log(LOG_INFO, "[WiFi] IP Address: " + WiFi.localIP().toString());
        logger.log(LOG_INFO, "[WiFi] Hostname: " + config.wifi.hostname);
        logger.log(LOG_INFO, "[WiFi] RSSI: " + String(WiFi.RSSI()) + " dBm");
    } else if (config.wifi.ap_fallback_enabled) {
        logger.log(LOG_WARN, "[WiFi] Connection failed - starting AP mode");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(config.wifi.ap_ssid.c_str(), config.wifi.ap_password.c_str());
        logger.log(LOG_INFO, "[WiFi] AP Mode started ✓");
        logger.log(LOG_INFO, "[WiFi] AP SSID: " + config.wifi.ap_ssid);
        logger.log(LOG_INFO, "[WiFi] AP IP: " + WiFi.softAPIP().toString());
    } else {
        logger.log(LOG_ERROR, "[WiFi] Connection failed and AP fallback disabled");
    }

    xSemaphoreGive(configMutex);
}

// ===================================================================================
// SPIFFS Initialization
// ===================================================================================
void initializeSPIFFS() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   SPIFFS Filesystem");
    logger.log(LOG_INFO, "========================================");

    logger.log(LOG_INFO, "[SPIFFS] Mounting filesystem...");

    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }

    if (!SPIFFS.begin(true)) {
        logger.log(LOG_ERROR, "[SPIFFS] Mount failed! Attempting format...");

        if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            Watchdog.feed();
            xSemaphoreGive(feedMutex);
        }

        if (!SPIFFS.format()) {
            logger.log(LOG_ERROR, "[SPIFFS] Format failed! Continuing without filesystem...");
            return;
        }

        logger.log(LOG_INFO, "[SPIFFS] Formatted successfully");

        if (!SPIFFS.begin()) {
            logger.log(LOG_ERROR, "[SPIFFS] Mount failed even after format!");
            return;
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
}

// ===================================================================================
// Bridge Initialization
// ===================================================================================
void initializeBridge() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   Bridge Initialization");
    logger.log(LOG_INFO, "========================================");

    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }

    if (!bridge.begin()) {
        logger.log(LOG_ERROR, "[BRIDGE] Initialization failed!");
        logger.log(LOG_WARN, "[BRIDGE] Continuing without bridge (web interface still available)");

        if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            logger.log(LOG_DEBUG, "  UART RX: GPIO" + String(config.hardware.uart.rx_pin));
            logger.log(LOG_DEBUG, "  UART TX: GPIO" + String(config.hardware.uart.tx_pin));
            logger.log(LOG_DEBUG, "  CAN TX: GPIO" + String(config.hardware.can.tx_pin));
            logger.log(LOG_DEBUG, "  CAN RX: GPIO" + String(config.hardware.can.rx_pin));
            xSemaphoreGive(configMutex);
        }
    } else {
        logger.log(LOG_INFO, "[BRIDGE] Initialized successfully ✓");

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
}

// ===================================================================================
// Config Editor Initialization
// ===================================================================================
void initializeConfigEditor() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   TinyBMS Config Editor");
    logger.log(LOG_INFO, "========================================");

    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }

    configEditor.begin();
    logger.log(LOG_INFO, "[CONFIG_EDITOR] Initialized ✓");
}

// ===================================================================================
// Global System Initialization
// ===================================================================================
void initializeSystem() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   System Initialization");
    logger.log(LOG_INFO, "========================================");

    logger.log(LOG_INFO, "[CONFIG] Loading configuration from SPIFFS...");

    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (!config.begin("/config.json")) {
            logger.log(LOG_WARN, "[CONFIG] Using default configuration");
        }
        xSemaphoreGive(configMutex);
    } else {
        logger.log(LOG_ERROR, "[CONFIG] Failed to acquire config mutex");
    }

    initializeSPIFFS();
    initializeWiFi();

    liveDataQueue = xQueueCreate(LIVE_DATA_QUEUE_SIZE, sizeof(TinyBMS_LiveData));
    if (liveDataQueue == NULL) {
        logger.log(LOG_ERROR, "[INIT] Failed to create liveDataQueue");
    } else {
        logger.log(LOG_INFO, "[INIT] liveDataQueue created ✓");
    }

    initializeBridge();
    initializeConfigEditor();

    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }

    initWebServerTask();

    if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        Watchdog.feed();
        xSemaphoreGive(feedMutex);
    }

    logger.log(LOG_INFO, "[INIT] All subsystems initialized successfully ✓");
}