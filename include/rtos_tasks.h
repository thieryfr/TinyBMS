/**
 * @file rtos_tasks.h
 * @brief FreeRTOS task declarations and global objects for TinyBMS-Victron Bridge
 * @version 1.0
 *
 * This file declares all FreeRTOS task functions and global objects
 * shared across the project.
 */

#ifndef RTOS_TASKS_H
#define RTOS_TASKS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <ESPAsyncWebServer.h>

// ====================================================================================
// GLOBAL OBJECTS
// ====================================================================================

// Web Server
extern AsyncWebServer server;
extern AsyncWebSocket ws;

// FreeRTOS Synchronization
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;

// ====================================================================================
// TASK DECLARATIONS
// ====================================================================================

/**
 * @brief Web server task - Handles HTTP requests
 */
void webServerTask(void *pvParameters);

/**
 * @brief WebSocket task - Broadcasts live data to connected clients
 */
void websocketTask(void *pvParameters);

/**
 * @brief Initialize the dedicated web server task
 */
bool initWebServerTask();

#endif // RTOS_TASKS_H
