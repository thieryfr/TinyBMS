/**
 * @file esp_http_server_wrapper.h
 * @brief ESP-IDF HTTP Server wrapper for Phase 3
 *
 * Provides compatibility layer between AsyncWebServer and esp_http_server
 */

#pragma once

#ifdef USE_ESP_IDF_WEBSERVER

#include "esp_http_server.h"
#include "esp_log.h"
#include <functional>
#include <map>
#include <string>

namespace tinybms {
namespace web {

// Forward declarations
class HttpServerIDF;
class HttpRequestIDF;

// Request handler type
using RequestHandlerIDF = std::function<void(HttpRequestIDF*)>;

/**
 * @brief HTTP Request wrapper for ESP-IDF
 */
class HttpRequestIDF {
public:
    HttpRequestIDF(httpd_req_t* req) : req_(req), status_sent_(false) {}

    // Send JSON response
    void send(int status, const char* contentType, const String& content) {
        httpd_resp_set_status(req_, getStatusString(status));
        httpd_resp_set_type(req_, contentType);
        httpd_resp_set_hdr(req_, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(req_, content.c_str(), content.length());
        status_sent_ = true;
    }

    // Get query parameter
    bool hasParam(const char* name) const {
        size_t buf_len = httpd_req_get_url_query_len(req_) + 1;
        if (buf_len <= 1) return false;

        char* buf = (char*)malloc(buf_len);
        if (!buf) return false;

        if (httpd_req_get_url_query_str(req_, buf, buf_len) == ESP_OK) {
            char param[64];
            esp_err_t err = httpd_query_key_value(buf, name, param, sizeof(param));
            free(buf);
            return (err == ESP_OK);
        }

        free(buf);
        return false;
    }

    String getParam(const char* name) const {
        size_t buf_len = httpd_req_get_url_query_len(req_) + 1;
        if (buf_len <= 1) return String();

        char* buf = (char*)malloc(buf_len);
        if (!buf) return String();

        String result;
        if (httpd_req_get_url_query_str(req_, buf, buf_len) == ESP_OK) {
            char param[256];
            if (httpd_query_key_value(buf, name, param, sizeof(param)) == ESP_OK) {
                result = String(param);
            }
        }

        free(buf);
        return result;
    }

    // Get POST body
    String getBody() {
        int total_len = req_->content_len;
        int cur_len = 0;
        char* buf = (char*)malloc(total_len + 1);
        if (!buf) return String();

        int received = 0;
        while (cur_len < total_len) {
            received = httpd_req_recv(req_, buf + cur_len, total_len - cur_len);
            if (received <= 0) {
                free(buf);
                return String();
            }
            cur_len += received;
        }
        buf[total_len] = '\0';

        String result(buf);
        free(buf);
        return result;
    }

    // Compatibility aliases for AsyncWebServer API
    bool hasArg(const char* name) const { return hasParam(name); }
    String arg(const char* name) const { return getParam(name); }
    const char* uri() const { return req_ ? req_->uri : ""; }

    httpd_req_t* getNative() { return req_; }

private:
    httpd_req_t* req_;
    bool status_sent_;

    const char* getStatusString(int status) {
        switch (status) {
            case 200: return "200 OK";
            case 400: return "400 Bad Request";
            case 404: return "404 Not Found";
            case 500: return "500 Internal Server Error";
            default: return "200 OK";
        }
    }
};

/**
 * @brief HTTP Server wrapper for ESP-IDF
 */
class HttpServerIDF {
public:
    HttpServerIDF(uint16_t port = 80) : server_(nullptr), port_(port) {}

    ~HttpServerIDF() {
        if (server_) {
            httpd_stop(server_);
        }
    }

    bool begin() {
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = port_;
        config.max_uri_handlers = 32;
        config.max_resp_headers = 16;
        config.stack_size = 8192;
        config.task_priority = 5;
        config.core_id = 0;
        config.max_open_sockets = 7;
        config.lru_purge_enable = true;

        esp_err_t err = httpd_start(&server_, &config);
        if (err != ESP_OK) {
            ESP_LOGE("HttpServerIDF", "Failed to start server: %s", esp_err_to_name(err));
            return false;
        }

        ESP_LOGI("HttpServerIDF", "HTTP server started on port %d", port_);
        return true;
    }

    void on(const char* uri, httpd_method_t method, RequestHandlerIDF handler) {
        auto* ctx = new RouteContext{this, handler};

        httpd_uri_t uri_handler = {
            .uri = uri,
            .method = method,
            .handler = routeDispatch,
            .user_ctx = ctx
        };

        httpd_register_uri_handler(server_, &uri_handler);
    }

    void onNotFound(RequestHandlerIDF handler) {
        notFoundHandler_ = handler;
    }

    httpd_handle_t getNative() { return server_; }

private:
    httpd_handle_t server_;
    uint16_t port_;
    RequestHandlerIDF notFoundHandler_;

    struct RouteContext {
        HttpServerIDF* server;
        RequestHandlerIDF handler;
    };

    static esp_err_t routeDispatch(httpd_req_t* req) {
        RouteContext* ctx = (RouteContext*)req->user_ctx;
        HttpRequestIDF request(req);
        ctx->handler(&request);
        return ESP_OK;
    }
};

} // namespace web
} // namespace tinybms

#endif // USE_ESP_IDF_WEBSERVER
