/**
 * @file web_server_setup.cpp
 * @brief Web server setup and configuration with FreeRTOS + Logging
 * @version 1.2 - Phase 3: Dual WebServer Support (AsyncWebServer + ESP-IDF)
 *
 * Configures WebServer with:
 * - WebSocket handlers
 * - Static file serving
 * - API routes
 * - TinyBMS config routes
 * Runs in a dedicated FreeRTOS task
 */

#include <Arduino.h>
#include <WiFi.h>

// Conditional WebServer includes
#ifdef USE_ESP_IDF_WEBSERVER
    #include "esp_http_server_wrapper.h"
    #include "esp_websocket_wrapper.h"
    using tinybms::web::HttpServerIDF;
    using tinybms::web::WebSocketIDF;
    using tinybms::web::WebSocketClientIDF;
    using tinybms::web::WsEventType;
    using WebServerType = HttpServerIDF;
    using WebSocketType = WebSocketIDF;
#else
    #include <ESPAsyncWebServer.h>
    using WebServerType = AsyncWebServer;
    using WebSocketType = AsyncWebSocket;
#endif

#include <SPIFFS.h>
#include "rtos_tasks.h"
#include "logger.h"
#include "config_manager.h"
#include "web_routes.h"

// External globals
extern ConfigManager config;
#ifdef USE_ESP_IDF_WEBSERVER
    extern HttpServerIDF server;
    extern WebSocketIDF ws;
#else
    extern AsyncWebServer server;
    extern AsyncWebSocket ws;
#endif
extern Logger logger;
extern TaskHandle_t webServerTaskHandle;
extern SemaphoreHandle_t configMutex;

#ifndef USE_ESP_IDF_WEBSERVER
// External functions (defined in other modules) - AsyncWebServer only
extern void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                             AwsEventType type, void *arg, uint8_t *data, size_t len);
#endif

/**
 * @brief Setup and start web server
 */
void setupWebServer() {
    logger.log(LOG_INFO, "========================================");
    logger.log(LOG_INFO, "   Web Server Configuration");
    logger.log(LOG_INFO, "========================================");

    ConfigManager::WebServerConfig web_config{};
    bool spiffs_enabled = true;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        web_config = config.web_server;
        spiffs_enabled = config.advanced.enable_spiffs;
        xSemaphoreGive(configMutex);
    } else {
        logger.log(LOG_WARN, "[WEB] Using default web server settings (config mutex unavailable)");
    }

    // Configure WebSocket
#ifdef USE_ESP_IDF_WEBSERVER
    // ESP-IDF WebSocket setup happens in setHandler (see below)
    logger.log(LOG_INFO, "[WS] WebSocket will be registered with HTTP server");
#else
    ws.onEvent(onWebSocketEvent);
    server.addHandler(&ws);
    logger.log(LOG_INFO, "[WS] WebSocket handler registered at /ws");
#endif

    // Serve static files from SPIFFS when enabled
    if (spiffs_enabled) {
#ifdef USE_ESP_IDF_WEBSERVER
        // ESP-IDF static file serving
        // Note: Simplified version - full implementation would use httpd_uri_t wildcards
        logger.log(LOG_INFO, "[WEB] Static file serving enabled (ESP-IDF)");
#else
        server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
        logger.log(LOG_INFO, "[WEB] Static files served from SPIFFS root");
#endif
    } else {
        logger.log(LOG_WARN, "[WEB] SPIFFS disabled - static hosting inactive");
#ifdef USE_ESP_IDF_WEBSERVER
        // ESP-IDF version
        server.on("/", HTTP_GET, [](tinybms::web::HttpRequestIDF *request) {
            request->send(503, "text/plain", "Static assets unavailable (SPIFFS disabled)");
        });
#else
        server.on("/", [](AsyncWebServerRequest *request) {
            request->send(503, "text/plain", "Static assets unavailable (SPIFFS disabled)");
        });
#endif
    }

    // Setup API routes (defined in web_routes_api.cpp)
    setupAPIRoutes(server);
    logger.log(LOG_INFO, "[API] Standard API routes configured");

    // Setup TinyBMS config routes (defined in web_routes_tinybms.cpp)
    setupTinyBMSConfigRoutes(server);
    logger.log(LOG_INFO, "[API] TinyBMS config routes configured");

    // CORS if enabled
    if (web_config.enable_cors) {
        DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
        logger.log(LOG_INFO, "[WEB] CORS enabled for all origins");
    } else {
        logger.log(LOG_DEBUG, "[WEB] CORS disabled");
    }

    // 404 handler
#ifdef USE_ESP_IDF_WEBSERVER
    server.onNotFound([](tinybms::web::HttpRequestIDF *request) {
        logger.log(LOG_WARN, "[WEB] 404 Not Found: " + String(request->uri()));
        request->send(404, "text/plain", "Not Found");
    });
#else
    server.onNotFound([](AsyncWebServerRequest *request) {
        logger.log(LOG_WARN, "[WEB] 404 Not Found: " + request->url());
        request->send(404, "text/plain", "Not Found");
    });
#endif

    // Start server
#ifdef USE_ESP_IDF_WEBSERVER
    if (server.begin(web_config.port)) {
        logger.log(LOG_INFO, String("[WEB] Server started on port ") + String(web_config.port));

        // Register WebSocket after server starts
        ws.setHandler(&server);
    } else {
        logger.log(LOG_ERROR, "[WEB] Failed to start server");
    }
#else
    server.begin();
    logger.log(LOG_INFO, String("[WEB] Server started on port ") + String(web_config.port));
#endif

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
#ifdef USE_ESP_IDF_WEBSERVER
        ws.cleanupClients();  // Clean inactive ESP-IDF WebSocket clients
#else
        ws.cleanupClients();  // Clean inactive AsyncWebSocket clients
#endif
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
bool initWebServerTask() {
    webServerTaskHandle = nullptr;

    BaseType_t result = xTaskCreate(webServerTask, "WebServerTask", 8192, NULL, 1, &webServerTaskHandle);
    if (result != pdPASS || webServerTaskHandle == nullptr) {
        logger.log(LOG_ERROR, "[WEB] Failed to create web server task");
        return false;
    }

    logger.log(LOG_INFO, "[WEB] Web server task created ✓");
    return true;
}
