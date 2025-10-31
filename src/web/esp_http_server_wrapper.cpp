/**
 * @file esp_http_server_wrapper.cpp
 * @brief ESP-IDF HTTP Server wrapper implementation
 */

#ifdef USE_ESP_IDF_WEBSERVER

#include "esp_http_server_wrapper.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <mbedtls/base64.h>
#include <sys/stat.h>

namespace tinybms {
namespace web {

static const char* TAG = "HttpServerIDF";

// ============================================================================
// HttpRequestIDF Implementation
// ============================================================================

HttpRequestIDF::HttpRequestIDF(HttpServerIDF* server, httpd_req_t* req)
    : server_(server), req_(req), body_read_(false), method_(HTTP_GET) {
    if (req_) {
        method_ = req_->method;
        uri_ = req_->uri ? req_->uri : "";

        // Parse query string parameters once at construction
        size_t query_len = httpd_req_get_url_query_len(req_);
        if (query_len > 0) {
            std::string query;
            query.resize(query_len + 1);
            if (httpd_req_get_url_query_str(req_, query.data(), query.size()) == ESP_OK) {
                parseQueryString(query.c_str());
            }
        }
    }
}

void HttpRequestIDF::send(int status, const char* contentType, const String& content) {
    if (!req_) {
        return;
    }

    httpd_resp_set_status(req_, statusToString(status));
    httpd_resp_set_type(req_, contentType);

    if (server_) {
        server_->applyCors(req_);
    }

    httpd_resp_send(req_, content.c_str(), content.length());
}

bool HttpRequestIDF::hasArg(const char* name) const {
    return hasParam(name);
}

bool HttpRequestIDF::hasParam(const char* name) const {
    return params_.find(String(name)) != params_.end();
}

String HttpRequestIDF::arg(const char* name) const {
    return getParam(name);
}

String HttpRequestIDF::getParam(const char* name) const {
    auto it = params_.find(String(name));
    return (it != params_.end()) ? it->second : String();
}

String HttpRequestIDF::getBody() {
    if (!req_ || body_read_) {
        return body_;
    }

    size_t content_len = req_->content_len;
    if (content_len == 0) {
        body_read_ = true;
        return body_;
    }

    std::string buffer;
    buffer.resize(std::max<size_t>(content_len, 1024) + 1);

    int remaining = static_cast<int>(content_len);
    while (remaining > 0) {
        int to_read = std::min(remaining, static_cast<int>(buffer.size() - 1));
        int received = httpd_req_recv(req_, buffer.data(), to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            break;
        }
        buffer[received] = '\0';
        body_ += String(buffer.c_str());
        remaining -= received;
    }

    body_read_ = true;
    return body_;
}

String HttpRequestIDF::header(const char* name) const {
    if (!req_ || !name) {
        return String();
    }

    size_t len = httpd_req_get_hdr_value_len(req_, name);
    if (len == 0) {
        return String();
    }

    std::string value;
    value.resize(len + 1);
    if (httpd_req_get_hdr_value_str(req_, name, value.data(), value.size()) != ESP_OK) {
        return String();
    }

    return String(value.c_str());
}

httpd_method_t HttpRequestIDF::method() const {
    return method_;
}

const char* HttpRequestIDF::uri() const {
    return uri_.c_str();
}

httpd_req_t* HttpRequestIDF::getNative() {
    return req_;
}

void HttpRequestIDF::parseQueryString(const char* query) {
    if (!query) {
        return;
    }

    String q(query);
    int start = 0;
    while (start < q.length()) {
        int amp_pos = q.indexOf('&', start);
        if (amp_pos < 0) {
            amp_pos = q.length();
        }

        int eq_pos = q.indexOf('=', start);
        if (eq_pos >= 0 && eq_pos < amp_pos) {
            String key = q.substring(start, eq_pos);
            String value = q.substring(eq_pos + 1, amp_pos);
            params_[key] = value;
        }

        start = amp_pos + 1;
    }
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

// ============================================================================
// HttpServerIDF Implementation
// ============================================================================

HttpServerIDF::HttpServerIDF()
    : server_(nullptr),
      port_(80),
      notFoundHandler_(nullptr),
      cors_enabled_(false),
      cors_allow_origin_("*"),
      cors_allow_headers_("Content-Type, Authorization"),
      cors_allow_methods_("GET,POST,PUT,DELETE,OPTIONS"),
      cors_allow_credentials_(true),
      options_registered_(false),
      auth_enabled_(false) {}

HttpServerIDF::HttpServerIDF(uint16_t port)
    : HttpServerIDF() {
    port_ = port;
}

HttpServerIDF::~HttpServerIDF() {
    stop();
}

bool HttpServerIDF::begin() {
    return begin(port_);
}

bool HttpServerIDF::begin(uint16_t port) {
    port_ = port;

    if (server_) {
        stop();
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = port_ + 1;
    config.max_uri_handlers = 32;
    config.max_resp_headers = 16;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.core_id = 0;
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.global_user_ctx = this;

    esp_err_t err = httpd_start(&server_, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        server_ = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "HTTP server started on port %d", port_);

    registerAllHandlers();
    return true;
}

void HttpServerIDF::stop() {
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
        options_registered_ = false;
    }
}

void HttpServerIDF::on(const char* uri, httpd_method_t method, RequestHandlerIDF handler) {
    if (!uri || !handler) {
        return;
    }

    auto ctx = std::make_unique<RouteHandler>();
    ctx->server = this;
    ctx->uri = uri;
    ctx->method = method;
    ctx->handler = handler;

    RouteHandler* raw = ctx.get();
    route_handlers_.push_back(std::move(ctx));

    if (server_) {
        registerRouteHandler(raw);
    }
}

void HttpServerIDF::onNotFound(RequestHandlerIDF handler) {
    notFoundHandler_ = handler;
    if (server_) {
        httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND, notFoundDispatcher);
    }
}

void HttpServerIDF::serveStatic(const char* uri, const char* path, const char* defaultFile) {
    auto route = std::make_unique<StaticRoute>();
    route->server = this;

    if (uri && std::strlen(uri) > 0) {
        route->mount_uri = uri;
    } else {
        route->mount_uri = "/";
    }

    if (route->mount_uri.size() > 1 && route->mount_uri.back() == '/') {
        route->mount_uri.pop_back();
    }

    if (path && std::strlen(path) > 0) {
        route->fs_base = path;
    } else {
        route->fs_base = "/spiffs";
    }

    if (!route->fs_base.empty() && route->fs_base.back() == '/') {
        route->fs_base.pop_back();
    }

    if (defaultFile) {
        route->default_file = defaultFile;
    }

    route->uri_pattern = route->mount_uri;
    if (route->uri_pattern.empty() || route->uri_pattern == "/") {
        route->uri_pattern = "/*";
    } else if (route->uri_pattern.back() == '*') {
        // already wildcard
    } else if (route->uri_pattern.back() == '/') {
        route->uri_pattern += "*";
    } else {
        route->uri_pattern += "/*";
    }

    StaticRoute* raw = route.get();
    static_routes_.push_back(std::move(route));

    if (server_) {
        registerStaticHandler(raw);
    }
}

void HttpServerIDF::enableCors(bool enable,
                               const char* allow_origin,
                               const char* allow_headers,
                               const char* allow_methods,
                               bool allow_credentials) {
    cors_enabled_ = enable;
    cors_allow_origin_ = allow_origin ? allow_origin : "*";
    cors_allow_headers_ = allow_headers ? allow_headers : "Content-Type, Authorization";
    cors_allow_methods_ = allow_methods ? allow_methods : "GET,POST,PUT,DELETE,OPTIONS";
    cors_allow_credentials_ = allow_credentials;

    if (server_) {
        registerOptionsHandler();
    }
}

void HttpServerIDF::disableCors() {
    cors_enabled_ = false;
}

void HttpServerIDF::enableBasicAuth(const char* username, const char* password) {
    if (!username || !password) {
        disableAuth();
        return;
    }

    std::string credentials = std::string(username) + ":" + std::string(password);

    size_t encoded_length = 0;
    int ret = mbedtls_base64_encode(nullptr, 0, &encoded_length,
                                    reinterpret_cast<const unsigned char*>(credentials.data()),
                                    credentials.size());
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "Failed to size base64 buffer (%d)", ret);
        disableAuth();
        return;
    }

    std::string encoded;
    encoded.resize(encoded_length);

    ret = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(encoded.data()), encoded.size(),
                                &encoded_length,
                                reinterpret_cast<const unsigned char*>(credentials.data()),
                                credentials.size());
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to encode credentials (%d)", ret);
        disableAuth();
        return;
    }

    encoded.resize(encoded_length);
    auth_header_ = "Basic " + encoded;
    auth_enabled_ = true;
}

void HttpServerIDF::disableAuth() {
    auth_enabled_ = false;
    auth_header_.clear();
}

httpd_handle_t HttpServerIDF::getNative() const {
    return server_;
}

HttpServerIDF* HttpServerIDF::fromRequest(httpd_req_t* req) {
    if (!req || !req->handle) {
        return nullptr;
    }
    return static_cast<HttpServerIDF*>(httpd_get_global_user_ctx(req->handle));
}

void HttpServerIDF::registerAllHandlers() {
    if (!server_) {
        return;
    }

    options_registered_ = false;

    for (auto& route : route_handlers_) {
        registerRouteHandler(route.get());
    }

    for (auto& route : static_routes_) {
        registerStaticHandler(route.get());
    }

    if (notFoundHandler_) {
        httpd_register_err_handler(server_, HTTPD_404_NOT_FOUND, notFoundDispatcher);
    }

    registerOptionsHandler();
}

void HttpServerIDF::registerRouteHandler(RouteHandler* route) {
    if (!route || !server_) {
        return;
    }

    httpd_uri_t uri_handler = {
        .uri = route->uri.c_str(),
        .method = route->method,
        .handler = routeDispatcher,
        .user_ctx = route,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
        .recv_msg = nullptr,
        .process_ws_control_frames = nullptr,
    };

    uri_handler.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_register_uri_handler(server_, &uri_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register route %s: %s", route->uri.c_str(), esp_err_to_name(err));
    }
}

void HttpServerIDF::registerStaticHandler(StaticRoute* route) {
    if (!route || !server_) {
        return;
    }

    httpd_uri_t get_handler = {
        .uri = route->uri_pattern.c_str(),
        .method = HTTP_GET,
        .handler = staticFileDispatcher,
        .user_ctx = route,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
        .recv_msg = nullptr,
        .process_ws_control_frames = nullptr,
    };
    get_handler.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_register_uri_handler(server_, &get_handler);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register static route %s: %s", route->uri_pattern.c_str(), esp_err_to_name(err));
    }
}

void HttpServerIDF::registerOptionsHandler() {
    if (!server_ || !cors_enabled_ || options_registered_) {
        return;
    }

    httpd_uri_t options_handler = {
        .uri = "/*",
        .method = HTTP_OPTIONS,
        .handler = optionsDispatcher,
        .user_ctx = this,
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
        .recv_msg = nullptr,
        .process_ws_control_frames = nullptr,
    };

    options_handler.uri_match_fn = httpd_uri_match_wildcard;

    if (httpd_register_uri_handler(server_, &options_handler) == ESP_OK) {
        options_registered_ = true;
    }
}

void HttpServerIDF::applyCors(httpd_req_t* req) const {
    if (!cors_enabled_ || !req) {
        return;
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", cors_allow_origin_.c_str());
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", cors_allow_methods_.c_str());
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", cors_allow_headers_.c_str());
    if (cors_allow_credentials_) {
        httpd_resp_set_hdr(req, "Access-Control-Allow-Credentials", "true");
    }
}

bool HttpServerIDF::checkAuthorization(httpd_req_t* req) const {
    if (!auth_enabled_ || !req) {
        return true;
    }

    if (req->method == HTTP_OPTIONS) {
        // Allow CORS preflight without credentials
        return true;
    }

    size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0) {
        return false;
    }

    std::string header;
    header.resize(len + 1);
    if (httpd_req_get_hdr_value_str(req, "Authorization", header.data(), header.size()) != ESP_OK) {
        return false;
    }

    header.resize(len);
    return header == auth_header_;
}

void HttpServerIDF::rejectUnauthorized(httpd_req_t* req) const {
    if (!req) {
        return;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "text/plain");
    applyCors(req);
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"TinyBMS\"");
    httpd_resp_send(req, "Authentication required", HTTPD_RESP_USE_STRLEN);
}

esp_err_t HttpServerIDF::serveStaticFile(StaticRoute* route, httpd_req_t* req) {
    if (!route || !req) {
        return ESP_FAIL;
    }

    bool used_default = false;
    std::string full_path = buildFilePath(route, req->uri, &used_default);

    if (full_path.empty()) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        applyCors(req);
        httpd_resp_send(req, "Not Found", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    FILE* file = fopen(full_path.c_str(), "rb");
    if (!file && !route->default_file.empty() && !used_default) {
        // Attempt SPA fallback to default document if request looks like a route
        if (!std::strchr(req->uri ? req->uri : "", '.')) {
            std::string fallback = route->fs_base;
            if (!fallback.empty() && fallback.back() != '/') {
                fallback += '/';
            }
            fallback += route->default_file;
            file = fopen(fallback.c_str(), "rb");
            if (file) {
                full_path = fallback;
                used_default = true;
            }
        }
    }

    if (!file) {
        int err = errno;
        ESP_LOGW(TAG, "Static file not found (%s): %s", strerror(err), full_path.c_str());
        httpd_resp_set_status(req, (err == ENOENT) ? "404 Not Found" : "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain");
        applyCors(req);
        const char* message = (err == ENOENT) ? "Not Found" : "Static content unavailable";
        httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    applyCors(req);

    const char* mime = mimeTypeForPath(full_path);
    httpd_resp_set_type(req, mime);
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=60");

    if (full_path.size() > 3 && full_path.compare(full_path.size() - 3, 3, ".gz") == 0) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    char buffer[1024];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        esp_err_t res = httpd_resp_send_chunk(req, buffer, read);
        if (res != ESP_OK) {
            fclose(file);
            return res;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

std::string HttpServerIDF::buildFilePath(const StaticRoute* route, const char* request_uri, bool* used_default) const {
    if (used_default) {
        *used_default = false;
    }

    std::string req_path = request_uri ? request_uri : "/";
    std::string base = route->mount_uri.empty() ? "/" : route->mount_uri;
    if (base.size() > 1 && base.back() == '/') {
        base.pop_back();
    }

    std::string relative;
    if (base == "/") {
        relative = req_path;
    } else if (req_path.rfind(base, 0) == 0) {
        relative = req_path.substr(base.size());
    } else {
        relative = req_path;
    }

    if (relative.empty() || relative == "/") {
        if (!route->default_file.empty()) {
            relative = "/" + route->default_file;
            if (used_default) {
                *used_default = true;
            }
        }
    } else if (relative.back() == '/') {
        if (!route->default_file.empty()) {
            relative += route->default_file;
            if (used_default) {
                *used_default = true;
            }
        }
    }

    if (relative.empty()) {
        return std::string();
    }

    if (relative.front() != '/') {
        relative.insert(relative.begin(), '/');
    }

    std::string fs_path = route->fs_base.empty() ? "/spiffs" : route->fs_base;
    if (!fs_path.empty() && fs_path.back() == '/' && relative.front() == '/') {
        fs_path.pop_back();
    }

    return fs_path + relative;
}

const char* HttpServerIDF::mimeTypeForPath(const std::string& path) const {
    auto endsWith = [&path](const char* suffix) {
        size_t len = std::strlen(suffix);
        if (len > path.size()) {
            return false;
        }
        return path.compare(path.size() - len, len, suffix) == 0;
    };

    if (endsWith(".html") || endsWith(".htm")) return "text/html";
    if (endsWith(".css")) return "text/css";
    if (endsWith(".js")) return "application/javascript";
    if (endsWith(".mjs")) return "application/javascript";
    if (endsWith(".json")) return "application/json";
    if (endsWith(".png")) return "image/png";
    if (endsWith(".jpg") || endsWith(".jpeg")) return "image/jpeg";
    if (endsWith(".gif")) return "image/gif";
    if (endsWith(".svg")) return "image/svg+xml";
    if (endsWith(".ico")) return "image/x-icon";
    if (endsWith(".woff")) return "font/woff";
    if (endsWith(".woff2")) return "font/woff2";
    if (endsWith(".ttf")) return "font/ttf";
    if (endsWith(".map")) return "application/json";
    if (endsWith(".txt")) return "text/plain";
    if (endsWith(".gz")) {
        // Check underlying type by stripping .gz
        std::string without_gz = path.substr(0, path.size() - 3);
        return mimeTypeForPath(without_gz);
    }

    return "application/octet-stream";
}

esp_err_t HttpServerIDF::routeDispatcher(httpd_req_t* req) {
    auto* route = static_cast<RouteHandler*>(req->user_ctx);
    if (!route || !route->handler) {
        return ESP_FAIL;
    }

    HttpServerIDF* server = route->server;
    if (server && !server->checkAuthorization(req)) {
        server->rejectUnauthorized(req);
        return ESP_OK;
    }

    HttpRequestIDF request(server, req);
    route->handler(&request);
    return ESP_OK;
}

esp_err_t HttpServerIDF::staticFileDispatcher(httpd_req_t* req) {
    auto* route = static_cast<StaticRoute*>(req->user_ctx);
    if (!route) {
        return ESP_FAIL;
    }

    HttpServerIDF* server = route->server;
    if (server && !server->checkAuthorization(req)) {
        server->rejectUnauthorized(req);
        return ESP_OK;
    }

    return server ? server->serveStaticFile(route, req) : ESP_FAIL;
}

esp_err_t HttpServerIDF::optionsDispatcher(httpd_req_t* req) {
    HttpServerIDF* server = fromRequest(req);
    if (!server) {
        return ESP_FAIL;
    }

    server->applyCors(req);
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

esp_err_t HttpServerIDF::notFoundDispatcher(httpd_req_t* req, httpd_err_code_t) {
    HttpServerIDF* server = fromRequest(req);
    if (!server || !server->notFoundHandler_) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
        return ESP_OK;
    }

    if (!server->checkAuthorization(req)) {
        server->rejectUnauthorized(req);
        return ESP_OK;
    }

    HttpRequestIDF request(server, req);
    server->notFoundHandler_(&request);
    return ESP_OK;
}

} // namespace web
} // namespace tinybms

#endif // USE_ESP_IDF_WEBSERVER
