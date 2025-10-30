/**
 * @file websocket_handlers.h
 * @brief WebSocket event handlers and notification functions
 * @version 1.0
 */

#ifndef WEBSOCKET_HANDLERS_H
#define WEBSOCKET_HANDLERS_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "shared_data.h"

/**
 * @brief WebSocket event handler
 */
void onWebSocketEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len);

/**
 * @brief Build status JSON from live data
 */
void buildStatusJSON(String& output, const TinyBMS_LiveData& data);

/**
 * @brief Notify all WebSocket clients with JSON data
 */
void notifyClients(const String& json);

#endif // WEBSOCKET_HANDLERS_H
