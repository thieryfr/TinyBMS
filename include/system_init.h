/**
 * @file system_init.h
 * @brief System initialization functions
 * @version 1.0
 */

#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

/**
 * @brief Initialize all system components (WiFi, SPIFFS, Bridge, Web Server)
 * @return true if every critical component was initialized successfully
 */
bool initializeSystem();

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
