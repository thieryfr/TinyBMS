/**
 * @file esp_websocket_wrapper.h
 * @brief ESP-IDF WebSocket wrapper for Phase 3
 */

#pragma once

#ifdef USE_ESP_IDF_WEBSERVER

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_http_server_wrapper.h"
#include <algorithm>
#include <functional>
#include <mutex>
#include <vector>

namespace tinybms {
namespace web {

// Forward declarations
class WebSocketIDF;
class WebSocketClientIDF;

// Event types
enum class WsEventType {
    Connect,
    Disconnect,
    Data,
    Pong,
    Error
};

// WebSocket event handler type
using WsEventHandlerIDF = std::function<void(WebSocketIDF*, WebSocketClientIDF*, WsEventType, uint8_t*, size_t)>;

/**
 * @brief WebSocket Client wrapper
 */
class WebSocketClientIDF {
public:
    WebSocketClientIDF(int fd) : fd_(fd), connected_(true) {}

    int id() const { return fd_; }
    bool isConnected() const { return connected_; }
    void setConnected(bool connected) { connected_ = connected; }

private:
    int fd_;
    bool connected_;
};

/**
 * @brief WebSocket Server wrapper for ESP-IDF
 */
class WebSocketIDF {
public:
    WebSocketIDF(const char* uri) : uri_(uri), server_(nullptr) {}

    void setHandler(httpd_handle_t server) {
        server_ = server;

        httpd_uri_t ws_uri = {
            .uri = uri_,
            .method = HTTP_GET,
            .handler = wsHandler,
            .user_ctx = this,
            .is_websocket = true,
            .handle_ws_control_frames = true
        };

        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI("WebSocketIDF", "WebSocket registered at %s", uri_);
    }

    void onEvent(WsEventHandlerIDF handler) {
        eventHandler_ = handler;
    }

    void textAll(const String& message) {
        textAll(message.c_str(), message.length());
    }

    void textAll(const char* message, size_t len) {
        std::lock_guard<std::mutex> lock(clientsMutex_);

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.payload = (uint8_t*)message;
        ws_pkt.len = len;
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;

        for (auto& client : clients_) {
            if (client.isConnected()) {
                esp_err_t ret = httpd_ws_send_frame_async(server_, client.id(), &ws_pkt);
                if (ret != ESP_OK) {
                    ESP_LOGW("WebSocketIDF", "Failed to send to client %d: %s",
                             client.id(), esp_err_to_name(ret));
                }
            }
        }
    }

    WebSocketClientIDF* attachClient(int fd) {
        WebSocketClientIDF* attached_client = nullptr;
        bool notify_connect = false;
        size_t active_clients = 0;

        {
            std::lock_guard<std::mutex> lock(clientsMutex_);

            auto it = std::find_if(
                clients_.begin(),
                clients_.end(),
                [fd](const WebSocketClientIDF& client) { return client.id() == fd; }
            );

            if (it == clients_.end()) {
                clients_.emplace_back(fd);
                attached_client = &clients_.back();
                notify_connect = true;
            } else {
                bool was_connected = it->isConnected();
                it->setConnected(true);
                attached_client = &(*it);
                notify_connect = !was_connected;
            }

            active_clients = std::count_if(
                clients_.begin(),
                clients_.end(),
                [](const WebSocketClientIDF& client) { return client.isConnected(); }
            );
        }

        if (notify_connect) {
            ESP_LOGI(
                "WebSocketIDF",
                "Client fd %d attached (active=%zu)",
                fd,
                active_clients
            );
        }

        if (notify_connect && eventHandler_) {
            eventHandler_(this, attached_client, WsEventType::Connect, nullptr, 0);
        }

        return attached_client;
    }

    void detachClient(int fd) {
        WebSocketClientIDF* detached_client = nullptr;
        bool notify_disconnect = false;
        size_t active_clients = 0;

        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            for (auto& client : clients_) {
                if (client.id() == fd) {
                    if (client.isConnected()) {
                        client.setConnected(false);
                        notify_disconnect = true;
                        detached_client = &client;
                    }
                    break;
                }
            }

            active_clients = std::count_if(
                clients_.begin(),
                clients_.end(),
                [](const WebSocketClientIDF& client) { return client.isConnected(); }
            );
        }

        if (notify_disconnect) {
            ESP_LOGI(
                "WebSocketIDF",
                "Client fd %d detached (active=%zu)",
                fd,
                active_clients
            );
        }

        if (notify_disconnect && eventHandler_) {
            eventHandler_(this, detached_client, WsEventType::Disconnect, nullptr, 0);
        }
    }

    size_t count() const {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        return clients_.size();
    }

    size_t connectedCount() const {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        return static_cast<size_t>(std::count_if(
            clients_.begin(),
            clients_.end(),
            [](const WebSocketClientIDF& client) { return client.isConnected(); }
        ));
    }

    void cleanupClients() {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(
            std::remove_if(clients_.begin(), clients_.end(),
                          [](const WebSocketClientIDF& c) { return !c.isConnected(); }),
            clients_.end()
        );
    }

private:
    const char* uri_;
    httpd_handle_t server_;
    WsEventHandlerIDF eventHandler_;
    std::vector<WebSocketClientIDF> clients_;
    mutable std::mutex clientsMutex_;

    static esp_err_t wsHandler(httpd_req_t* req) {
        HttpServerIDF* server = HttpServerIDF::fromRequest(req);
        if (server && !server->checkAuthorization(req)) {
            server->rejectUnauthorized(req);
            return ESP_FAIL;
        }

        WebSocketIDF* self = (WebSocketIDF*)req->user_ctx;
        if (!self) {
            ESP_LOGE("WebSocketIDF", "Missing WebSocket context during handshake");
            return ESP_ERR_INVALID_STATE;
        }

        int fd = httpd_req_to_sockfd(req);

        if (req->method == HTTP_GET) {
            ESP_LOGI("WebSocketIDF", "WebSocket handshake from fd %d", fd);
            self->attachClient(fd);
            return ESP_OK;
        }

        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK) {
            ESP_LOGE("WebSocketIDF", "recv_frame failed: %s", esp_err_to_name(ret));
            return ret;
        }

        if (ws_pkt.type == HTTPD_WS_TYPE_TEXT ||
            ws_pkt.type == HTTPD_WS_TYPE_BINARY ||
            ws_pkt.type == HTTPD_WS_TYPE_PING ||
            ws_pkt.type == HTTPD_WS_TYPE_PONG) {

            if (ws_pkt.len) {
                uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
                if (!buf) {
                    ESP_LOGE("WebSocketIDF", "Failed to allocate memory");
                    return ESP_ERR_NO_MEM;
                }

                ws_pkt.payload = buf;
                ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
                if (ret != ESP_OK) {
                    free(buf);
                    return ret;
                }

                // Handle message
                if (ws_pkt.type == HTTPD_WS_TYPE_TEXT) {
                    buf[ws_pkt.len] = '\0';
                    ESP_LOGD("WebSocketIDF", "Received: %s", buf);

                    self->attachClient(fd);

                    // Handle ping/pong
                    if (ws_pkt.len == 4 && memcmp(buf, "ping", 4) == 0) {
                        httpd_ws_frame_t pong;
                        memset(&pong, 0, sizeof(httpd_ws_frame_t));
                        const char* pong_msg = "pong";
                        pong.payload = (uint8_t*)pong_msg;
                        pong.len = 4;
                        pong.type = HTTPD_WS_TYPE_TEXT;
                        httpd_ws_send_frame_async(self->server_, fd, &pong);
                    }
                }

                free(buf);
            }
        } else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE) {
            ESP_LOGI("WebSocketIDF", "Client disconnected: fd=%d", fd);

            self->detachClient(fd);
        }

        return ESP_OK;
    }
};

} // namespace web
} // namespace tinybms

#endif // USE_ESP_IDF_WEBSERVER
