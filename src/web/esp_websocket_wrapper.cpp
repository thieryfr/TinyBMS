/**
 * @file esp_websocket_wrapper.cpp
 * @brief ESP-IDF WebSocket wrapper implementation
 *
 * Phase 3: Migration WebServer
 * Provides AsyncWebSocket-compatible API using esp_http_server WebSocket support
 */

#ifdef USE_ESP_IDF_WEBSERVER

#include "esp_websocket_wrapper.h"
#include "esp_log.h"

namespace tinybms {
namespace web {

// WebSocketIDF implementation is header-only, no additional implementation needed here
// The implementation is in esp_websocket_wrapper.h for simplicity

} // namespace web
} // namespace tinybms

#endif // USE_ESP_IDF_WEBSERVER
