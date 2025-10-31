/**
 * @file esp_http_server_wrapper.h
 * @brief ESP-IDF HTTP Server wrapper for Phase 3
 */

#pragma once

#ifdef USE_ESP_IDF_WEBSERVER

#include <Arduino.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace tinybms {
namespace web {

class HttpServerIDF;
class HttpRequestIDF;

// Request handler type compatible with AsyncWebServer lambdas
using RequestHandlerIDF = std::function<void(HttpRequestIDF*)>;

/**
 * @brief HTTP Request wrapper for ESP-IDF
 */
class HttpRequestIDF {
public:
    HttpRequestIDF(HttpServerIDF* server, httpd_req_t* req);

    void send(int status, const char* contentType, const String& content);

    bool hasArg(const char* name) const;
    bool hasParam(const char* name) const;
    String arg(const char* name) const;
    String getParam(const char* name) const;

    String getBody();
    String header(const char* name) const;

    httpd_method_t method() const;
    const char* uri() const;

    httpd_req_t* getNative();

private:
    HttpServerIDF* server_;
    httpd_req_t* req_;
    bool body_read_;
    httpd_method_t method_;
    String uri_;
    String body_;
    std::map<String, String> params_;

    void parseQueryString(const char* query);
    const char* statusToString(int code);
};

/**
 * @brief HTTP Server wrapper for ESP-IDF providing AsyncWebServer compatible API
 */
class HttpServerIDF {
public:
    HttpServerIDF();
    explicit HttpServerIDF(uint16_t port);
    ~HttpServerIDF();

    bool begin();
    bool begin(uint16_t port);
    void stop();

    void on(const char* uri, httpd_method_t method, RequestHandlerIDF handler);
    void onNotFound(RequestHandlerIDF handler);
    void serveStatic(const char* uri, const char* path, const char* defaultFile = nullptr);

    void enableCors(bool enable,
                    const char* allow_origin = "*",
                    const char* allow_headers = "Content-Type, Authorization",
                    const char* allow_methods = "GET,POST,PUT,DELETE,OPTIONS",
                    bool allow_credentials = true);
    void disableCors();

    void enableBasicAuth(const char* username, const char* password);
    void disableAuth();

    httpd_handle_t getNative() const;

    static HttpServerIDF* fromRequest(httpd_req_t* req);

    bool checkAuthorization(httpd_req_t* req) const;
    void rejectUnauthorized(httpd_req_t* req) const;

private:
    friend class HttpRequestIDF;

    struct RouteHandler {
        HttpServerIDF* server;
        std::string uri;
        httpd_method_t method;
        RequestHandlerIDF handler;
    };

    struct StaticRoute {
        HttpServerIDF* server;
        std::string mount_uri;
        std::string uri_pattern;
        std::string fs_base;
        std::string default_file;
    };

    httpd_handle_t server_;
    uint16_t port_;
    RequestHandlerIDF notFoundHandler_;
    std::vector<std::unique_ptr<RouteHandler>> route_handlers_;
    std::vector<std::unique_ptr<StaticRoute>> static_routes_;
    bool cors_enabled_;
    std::string cors_allow_origin_;
    std::string cors_allow_headers_;
    std::string cors_allow_methods_;
    bool cors_allow_credentials_;
    bool options_registered_;
    bool auth_enabled_;
    std::string auth_header_;

    void registerAllHandlers();
    void registerRouteHandler(RouteHandler* route);
    void registerStaticHandler(StaticRoute* route);
    void registerOptionsHandler();

    void applyCors(httpd_req_t* req) const;

    esp_err_t serveStaticFile(StaticRoute* route, httpd_req_t* req);
    std::string buildFilePath(const StaticRoute* route, const char* request_uri, bool* used_default) const;
    const char* mimeTypeForPath(const std::string& path) const;

    static esp_err_t routeDispatcher(httpd_req_t* req);
    static esp_err_t staticFileDispatcher(httpd_req_t* req);
    static esp_err_t optionsDispatcher(httpd_req_t* req);
    static esp_err_t notFoundDispatcher(httpd_req_t* req, httpd_err_code_t err);
};

} // namespace web
} // namespace tinybms

#endif // USE_ESP_IDF_WEBSERVER
