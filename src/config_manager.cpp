#include "config_manager.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "logger.h"
#include "event_bus.h"  // Phase 4: Event Bus integration

extern SemaphoreHandle_t configMutex;
extern Logger logger;
extern EventBus& eventBus;  // Phase 4: Event Bus instance

// ========================================================================
// CONFIG LOADING
// ========================================================================

bool ConfigManager::begin(const char* filename) {
    filename_ = filename;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "Config load failed: could not acquire configMutex");
        return false;
    }

    if (!SPIFFS.begin(true)) {
        logger.log(LOG_ERROR, "SPIFFS mount failed");
        xSemaphoreGive(configMutex);
        return false;
    }
    
    if (!SPIFFS.exists(filename_)) {
        logger.log(LOG_WARNING, String("Config file not found (") + filename_ + "), using defaults");
        loaded_ = false;
        xSemaphoreGive(configMutex);
        return false;
    }

    File file = SPIFFS.open(filename_, "r");
    if (!file) {
        logger.log(LOG_ERROR, String("Failed to open config file: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        logger.log(LOG_ERROR, String("JSON parse error: ") + error.c_str());
        xSemaphoreGive(configMutex);
        return false;
    }

    // Load all sections
    loadWiFiConfig(doc);
    loadHardwareConfig(doc);
    loadTinyBMSConfig(doc);
    loadVictronConfig(doc);
    loadCVLConfig(doc);
    loadWebServerConfig(doc);
    loadLoggingConfig(doc);
    loadAdvancedConfig(doc);

    loaded_ = true;
    logger.log(LOG_INFO, "Configuration loaded successfully");

    printConfig();

    // Phase 4: Publish config loaded event
    eventBus.publishConfigChange("*", "", "", SOURCE_ID_CONFIG_MANAGER);

    xSemaphoreGive(configMutex);
    return true;
}

// ========================================================================
// CONFIG SAVE
// ========================================================================

bool ConfigManager::save() {
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        logger.log(LOG_ERROR, "Config save failed: could not acquire configMutex");
        return false;
    }

    DynamicJsonDocument doc(4096);

    // Build JSON from current config
    saveWiFiConfig(doc);
    saveHardwareConfig(doc);
    saveTinyBMSConfig(doc);
    saveVictronConfig(doc);
    saveCVLConfig(doc);
    saveWebServerConfig(doc);
    saveLoggingConfig(doc);
    saveAdvancedConfig(doc);

    File file = SPIFFS.open(filename_, "w");
    if (!file) {
        logger.log(LOG_ERROR, String("Failed to open config file for writing: ") + filename_);
        xSemaphoreGive(configMutex);
        return false;
    }

    if (serializeJson(doc, file) == 0) {
        logger.log(LOG_ERROR, "Failed to write configuration to file");
        file.close();
        xSemaphoreGive(configMutex);
        return false;
    }

    file.close();
    logger.log(LOG_INFO, "Configuration saved successfully");

    // Phase 4: Publish config changed event (config path "*" means all config)
    eventBus.publishConfigChange("*", "", "", SOURCE_ID_CONFIG_MANAGER);

    xSemaphoreGive(configMutex);
    return true;
}

// ========================================================================
// PRINT CONFIG (for debugging)
// ========================================================================

void ConfigManager::printConfig() const {
    logger.log(LOG_DEBUG, "=== CONFIG LOADED ===");
    logger.log(LOG_DEBUG, "WiFi: SSID=" + wifi.ssid + " Hostname=" + wifi.hostname);
    logger.log(LOG_DEBUG, "UART: RX=" + String(hardware.uart.rx_pin) +
                              " TX=" + String(hardware.uart.tx_pin) +
                              " Baud=" + String(hardware.uart.baudrate));
    logger.log(LOG_DEBUG, "CAN: RX=" + String(hardware.can.rx_pin) +
                              " TX=" + String(hardware.can.tx_pin) +
                              " Bitrate=" + String(hardware.can.bitrate));
    logger.log(LOG_DEBUG, "Logging Level=" + String(logging.log_level));
}

// ========================================================================
// STATUS
// ========================================================================

bool ConfigManager::isLoaded() const {
    return loaded_;
}