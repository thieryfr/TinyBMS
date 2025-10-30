/**
 * @file esp_http_server_wrapper.cpp
 * @brief ESP-IDF HTTP Server wrapper implementation
 *
 * Phase 3: Migration WebServer
 * Provides AsyncWebServer-compatible API using esp_http_server
 */

#ifdef USE_ESP_IDF_WEBSERVER

#include "esp_http_server_wrapper.h"
#include "esp_log.h"
#include <cstring>

namespace tinybms {
namespace web {

static const char* TAG = "HttpServerIDF";

// ============================================================================
// HttpRequestIDF Implementation
// ============================================================================

HttpRequestIDF::HttpRequestIDF(httpd_req_t* req)
    : req_(req), body_read_(false), method_(HTTP_GET) {
    if (req) {
        method_ = req->method;
        uri_ = req->uri;

        // Parse query string
        size_t query_len = httpd_req_get_url_query_len(req);
        if (query_len > 0) {
            char* query_str = (char*)malloc(query_len + 1);
            if (query_str && httpd_req_get_url_query_str(req, query_str, query_len + 1) == ESP_OK) {
                parseQueryString(query_str);
            }
            free(query_str);
        }
    }
}

void HttpRequestIDF::send(int status, const char* contentType, const String& content) {
    if (!req_) return;

    httpd_resp_set_status(req_, statusToString(status));
    httpd_resp_set_type(req_, contentType);
    httpd_resp_send(req_, content.c_str(), content.length());
}

bool HttpRequestIDF::hasArg(const char* name) const {
    return params_.find(name) != params_.end();
}

bool HttpRequestIDF::hasParam(const char* name) const {
    return hasArg(name);
}

String HttpRequestIDF::arg(const char* name) const {
    auto it = params_.find(name);
    return (it != params_.end()) ? it->second : String();
}

String HttpRequestIDF::getParam(const char* name) const {
    return arg(name);
}

String HttpRequestIDF::getBody() {
    if (!req_ || body_read_) return body_;

    size_t content_len = req_->content_len;
    if (content_len == 0) return body_;

    body_.reserve(content_len + 1);

    char* buf = (char*)malloc(1024);
    if (!buf) return body_;

    int remaining = content_len;
    while (remaining > 0) {
        int recv_len = httpd_req_recv(req_, buf, std::min(remaining, 1024));
        if (recv_len <= 0) {
            if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            break;
        }
        body_ += String(buf, recv_len);
        remaining -= recv_len;
    }

    free(buf);
    body_read_ = true;
    return body_;
}

httpd_method_t HttpRequestIDF::method() const {
    return method_;
}

const char* HttpRequestIDF::statusToString(int code) {
    switch (code) {
        case 200: return "200 OK";
        case 201: return "201 Created";
        case 204: return "204 No Content";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 403: return "403 Forbidden";
        case 404: return "404 Not Found";
        case 500: return "500 Internal Server Error";
        case 503: return "503 Service Unavailable";
        default: return "200 OK";
    }
}

void HttpRequestIDF::parseQueryString(const char* query) {
    if (!query) return;

    String q(query);
    int start = 0;
    while (start < q.length()) {
        int amp_pos = q.indexOf('&', start);
        if (amp_pos < 0) amp_pos = q.length();

        int eq_pos = q.indexOf('=', start);
        if (eq_pos >= 0 && eq_pos < amp_pos) {
            String key = q.substring(start, eq_pos);
            String value = q.substring(eq_pos + 1, amp_pos);
            // URL decode would go here if needed
            params_[key] = value;
        }

        start = amp_pos + 1;
    }
}

// ============================================================================
// HttpServerIDF Implementation
// ============================================================================

HttpServerIDF::HttpServerIDF() : server_(nullptr) {
}

HttpServerIDF::~HttpServerIDF() {
    if (server_) {
        httpd_stop(server_);
    }
}

bool HttpServerIDF::begin(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.ctrl_port = port + 1;
    config.max_uri_handlers = 32;
    config.max_resp_headers = 8;
    config.stack_size = 8192;
    config.task_priority = 5;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", port);
    return true;
}

void HttpServerIDF::on(const char* uri, httpd_method_t method, RequestHandlerIDF handler) {
    if (!server_) return;

    // Store handler
    RouteHandler route;
    route.uri = uri;
    route.method = method;
    route.handler = handler;
    route.user_ctx = this;
    handlers_.push_back(route);

    // Register with ESP-IDF
    httpd_uri_t uri_handler = {
        .uri = handlers_.back().uri.c_str(),
        .method = method,
        .handler = staticHandler,
        .user_ctx = &handlers_.back()
    };

    httpd_register_uri_handler(server_, &uri_handler);
}

void HttpServerIDF::onNotFound(RequestHandlerIDF handler) {
    notFoundHandler_ = handler;

    if (server_) {
        httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND, static404Handler);
    }
}

void HttpServerIDF::serveStatic(const char* uri, const char* path, const char* defaultFile) {
    // Simplified static file serving
    // In a full implementation, this would register a handler that serves files from SPIFFS
    ESP_LOGW(TAG, "serveStatic not fully implemented: %s -> %s", uri, path);
}

esp_err_t HttpServerIDF::staticHandler(httpd_req_t* req) {
    RouteHandler* route = static_cast<RouteHandler*>(req->user_ctx);
    if (!route || !route->handler) {
        return ESP_FAIL;
    }

    HttpRequestIDF request(req);
    route->handler(&request);

    return ESP_OK;
}

esp_err_t HttpServerIDF::static404Handler(httpd_req_t* req, httpd_err_code_t err) {
    HttpServerIDF* self = static_cast<HttpServerIDF*>(httpd_get_global_user_ctx(req->handle));
    if (self && self->notFoundHandler_) {
        HttpRequestIDF request(req);
        self->notFoundHandler_(&request);
        return ESP_OK;
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
    return ESP_OK;
}

} // namespace web
} // namespace tinybms

#endif // USE_ESP_IDF_WEBSERVER
