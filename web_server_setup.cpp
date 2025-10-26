/**
 * @file web_server_setup.cpp
 * @brief Web server setup and configuration with FreeRTOS + Logging
 * @version 1.1 - Logging integration & structured initialization
 * 
 * Configures AsyncWebServer with:
 * - WebSocket handlers
 * - Static file serving
 * - API routes
 * - TinyBMS config routes
 * Runs in a dedicated FreeRTOS task
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <Freertos.h>
#include "rtos_tasks.h"
#include "logger.h"          // ✅ Added for logging
#include "config_manager.h"  // For access to config.web_server

// External globals
extern ConfigManager config;
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern Logger logger;

// External functions (defined in other modules)
extern void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, 
                             AwsEventType type, void *arg, uint8_t *data, size_t len);
extern void setupAPIRoutes(AsyncWebServer& server);
extern void setupTinyBMSConfigRoutes(AsyncWebServer& server);

/**
 * @brief Setup and start web server
 */
void setupWebServer() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   Web Server Configuration");
    logger.log(LOG_INFO, "========================================");

    // Configure WebSocket
    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
    logger.log(LOG_INFO, "[WS] WebSocket handler registered at /ws");

    // Serve static files from SPIFFS
    server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
    logger.log(LOG_INFO, "[WEB] Static files served from SPIFFS root");

    // Setup API routes (defined in web_routes_api.cpp)
    setupAPIRoutes(server);
    logger.log(LOG_INFO, "[API] Standard API routes configured");

    // Setup TinyBMS config routes (defined in web_routes_tinybms.cpp)
    setupTinyBMSConfigRoutes(server);
    logger.log(LOG_INFO, "[API] TinyBMS config routes configured");

    // CORS if enabled
    if (config.web_server.enable_cors) {
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        logger.log(LOG_INFO, "[WEB] CORS enabled for all origins");
    } else {
        logger.log(LOG_DEBUG, "[WEB] CORS disabled");
    }

    // 404 handler
    server.onNotFound([](AsyncWebServerRequest *request) {
        logger.log(LOG_WARN, "[WEB] 404 Not Found: " + request->url());
        request->send(404, "text/plain", "Not Found");
    });

    // Start server
    server.begin();
    logger.log(LOG_INFO, String("[WEB] Server started on port ") + String(config.web_server.port));

    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   ✓ Web Server Ready!");
    logger.log(LOG_INFO, "========================================");
}

/**
 * @brief FreeRTOS task for running the web server
 */
void webServerTask(void *pvParameters) {
    setupWebServer();

    while (true) {
        ws.cleanupClients();  // Clean inactive clients
        vTaskDelay(pdMS_TO_TICKS(1000)); // Vérification toutes les secondes

        // Optionnel : monitoring de stack pour debug
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
        logger.log(LOG_DEBUG, String("[TASK] webServerTask Stack High Water Mark: ") +
                             String(watermark * sizeof(StackType_t)) + " bytes");
    }
}

/**
 * @brief Initialize the web server task
 */
void initWebServerTask() {
    xTaskCreate(webServerTask, "WebServerTask", 8192, NULL, 1, NULL);
    logger.log(LOG_INFO, "[WEB] Web server task created ✓");
}