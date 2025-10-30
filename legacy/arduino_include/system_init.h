/**
 * @file system_init.h
 * @brief System initialization functions
 * @version 1.0
 */

#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "hal/hal_config.h"

class ConfigManager;

/**
 * @brief Initialize all system components (WiFi, SPIFFS, Bridge, Web Server)
 * @return true if every critical component was initialized successfully
 */
bool initializeSystem();

/**
 * @brief Build the HAL configuration structure from the runtime configuration
 */
hal::HalConfig buildHalConfig(const ConfigManager& cfg);

/**
 * @brief Initialize WiFi in STA mode or AP fallback
 */
bool initializeWiFi();

/**
 * @brief Initialize SPIFFS filesystem
 */
bool initializeSPIFFS();

/**
 * @brief Initialize TinyBMS-Victron bridge
 */
bool initializeBridge();

/**
 * @brief Initialize config editor (placeholder)
 */
bool initializeConfigEditor();

#endif // SYSTEM_INIT_H
