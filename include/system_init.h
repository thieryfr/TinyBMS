/**
 * @file system_init.h
 * @brief System initialization functions
 * @version 1.0
 */

#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

/**
 * @brief Initialize all system components (WiFi, SPIFFS, Bridge, Web Server)
 */
void initializeSystem();

/**
 * @brief Initialize WiFi in STA mode or AP fallback
 */
void initializeWiFi();

/**
 * @brief Initialize SPIFFS filesystem
 */
void initializeSPIFFS();

/**
 * @brief Initialize TinyBMS-Victron bridge
 */
void initializeBridge();

/**
 * @brief Initialize config editor (placeholder)
 */
void initializeConfigEditor();

#endif // SYSTEM_INIT_H
