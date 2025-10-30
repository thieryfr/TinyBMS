#include "http_server.hpp"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wifi_manager.hpp"
#include "system_config.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace tinybms {
namespace {
constexpr const char *TAG = "http";

struct ServerContext {
    SystemConfig *config;
    TinyBmsBridge *bridge;
};

void apply_cors(httpd_req_t *req, const SystemConfig &config) {
    if (!config.web.enable_cors) {
        return;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", config.web.cors_origin.c_str());
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

esp_err_t respond_with_json(httpd_req_t *req, cJSON *root, const SystemConfig &config) {
    char *rendered = cJSON_PrintUnformatted(root);
    if (!rendered) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
        return ESP_FAIL;
    }
    apply_cors(req, config);
    httpd_resp_set_type(req, "application/json");
    esp_err_t err = httpd_resp_send(req, rendered, HTTPD_RESP_USE_STRLEN);
    cJSON_free(rendered);
    return err;
}

cJSON *system_config_to_json(const SystemConfig &config) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_name", config.device_name.c_str());

    cJSON *ap = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "ap", ap);
    cJSON_AddStringToObject(ap, "ssid", config.ap.ssid.c_str());
    cJSON_AddStringToObject(ap, "password", config.ap.password.c_str());
    cJSON_AddNumberToObject(ap, "channel", config.ap.channel);
    cJSON_AddNumberToObject(ap, "max_connections", config.ap.max_connections);

    cJSON *sta = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "sta", sta);
    cJSON_AddBoolToObject(sta, "enabled", config.sta.enabled);
    cJSON_AddStringToObject(sta, "ssid", config.sta.ssid.c_str());
    cJSON_AddStringToObject(sta, "password", config.sta.password.c_str());

    cJSON *web = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "web", web);
    cJSON_AddBoolToObject(web, "enable_websocket", config.web.enable_websocket);
    cJSON_AddBoolToObject(web, "enable_cors", config.web.enable_cors);
    cJSON_AddStringToObject(web, "cors_origin", config.web.cors_origin.c_str());

    return root;
}

bool update_from_json(SystemConfig &config, const cJSON *root) {
    const cJSON *device = cJSON_GetObjectItemCaseSensitive(root, "device_name");
    if (cJSON_IsString(device) && device->valuestring) {
        config.device_name = device->valuestring;
    }

    const cJSON *ap = cJSON_GetObjectItemCaseSensitive(root, "ap");
    if (cJSON_IsObject(ap)) {
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(ap, "ssid");
        if (cJSON_IsString(ssid) && ssid->valuestring) {
            config.ap.ssid = ssid->valuestring;
        }
        const cJSON *password = cJSON_GetObjectItemCaseSensitive(ap, "password");
        if (cJSON_IsString(password) && password->valuestring) {
            config.ap.password = password->valuestring;
        }
        const cJSON *channel = cJSON_GetObjectItemCaseSensitive(ap, "channel");
        if (cJSON_IsNumber(channel)) {
            uint8_t ch = static_cast<uint8_t>(channel->valuedouble);
            if (ch >= 1 && ch <= 13) {
                config.ap.channel = ch;
            }
        }
        const cJSON *max_conn = cJSON_GetObjectItemCaseSensitive(ap, "max_connections");
        if (cJSON_IsNumber(max_conn)) {
            uint8_t mc = static_cast<uint8_t>(max_conn->valuedouble);
            if (mc >= 1 && mc <= 10) {
                config.ap.max_connections = mc;
            }
        }
    }

    const cJSON *sta = cJSON_GetObjectItemCaseSensitive(root, "sta");
    if (cJSON_IsObject(sta)) {
        const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(sta, "enabled");
        if (cJSON_IsBool(enabled)) {
            config.sta.enabled = cJSON_IsTrue(enabled);
        }
        const cJSON *ssid = cJSON_GetObjectItemCaseSensitive(sta, "ssid");
        if (cJSON_IsString(ssid) && ssid->valuestring) {
            config.sta.ssid = ssid->valuestring;
        }
        const cJSON *password = cJSON_GetObjectItemCaseSensitive(sta, "password");
        if (cJSON_IsString(password) && password->valuestring) {
            config.sta.password = password->valuestring;
        }
    }

    const cJSON *web = cJSON_GetObjectItemCaseSensitive(root, "web");
    if (cJSON_IsObject(web)) {
        const cJSON *ws = cJSON_GetObjectItemCaseSensitive(web, "enable_websocket");
        if (cJSON_IsBool(ws)) {
            config.web.enable_websocket = cJSON_IsTrue(ws);
        }
        const cJSON *cors = cJSON_GetObjectItemCaseSensitive(web, "enable_cors");
        if (cJSON_IsBool(cors)) {
            config.web.enable_cors = cJSON_IsTrue(cors);
        }
        const cJSON *origin = cJSON_GetObjectItemCaseSensitive(web, "cors_origin");
        if (cJSON_IsString(origin) && origin->valuestring) {
            config.web.cors_origin = origin->valuestring;
        }
    }

    return true;
}

esp_err_t handle_status(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000ULL);

    auto health = ctx->bridge->health_snapshot();
    cJSON *diag = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "diagnostics", diag);
    cJSON_AddNumberToObject(diag, "last_uart_delta_ms", health.last_uart_delta_ms);
    cJSON_AddNumberToObject(diag, "last_can_delta_ms", health.last_can_delta_ms);
    cJSON_AddNumberToObject(diag, "parsed_samples", health.parsed_samples);
    cJSON_AddNumberToObject(diag, "dropped_samples", health.dropped_samples);
    cJSON_AddNumberToObject(diag, "can_errors", health.can_errors);

    MeasurementSample sample{};
    if (ctx->bridge->latest_sample(sample)) {
        cJSON *payload = cJSON_CreateObject();
        cJSON_AddItemToObject(root, "last_sample", payload);
        cJSON_AddNumberToObject(payload, "timestamp_ms", sample.timestamp_ms);
        cJSON_AddNumberToObject(payload, "pack_voltage_v", sample.pack_voltage_v);
        cJSON_AddNumberToObject(payload, "pack_current_a", sample.pack_current_a);
        cJSON_AddNumberToObject(payload, "soc_percent", sample.soc_percent);
        cJSON_AddNumberToObject(payload, "temperature_c", sample.temperature_c);
    } else {
        cJSON_AddNullToObject(root, "last_sample");
    }

    esp_err_t err = respond_with_json(req, root, *ctx->config);
    cJSON_Delete(root);
    return err;
}

esp_err_t handle_config_get(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    cJSON *root = system_config_to_json(*ctx->config);
    esp_err_t err = respond_with_json(req, root, *ctx->config);
    cJSON_Delete(root);
    return err;
}

esp_err_t handle_config_post(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    std::vector<char> body;
    body.resize(req->content_len + 1);
    size_t received = 0;
    while (received < static_cast<size_t>(req->content_len)) {
        int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body.data());
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    SystemConfig new_config = *ctx->config;
    update_from_json(new_config, root);
    cJSON_Delete(root);

    esp_err_t err = save_system_config(new_config);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "persist failed");
        return err;
    }

    err = wifi_manager_update(new_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi update failed: %s", esp_err_to_name(err));
    }

    *ctx->config = new_config;
    cJSON *response = system_config_to_json(*ctx->config);
    err = respond_with_json(req, response, *ctx->config);
    cJSON_Delete(response);
    return err;
}

const char *mime_for_path(const std::string &path) {
    if (path.rfind(".html") != std::string::npos) {
        return "text/html";
    }
    if (path.rfind(".css") != std::string::npos) {
        return "text/css";
    }
    if (path.rfind(".js") != std::string::npos) {
        return "application/javascript";
    }
    if (path.rfind(".json") != std::string::npos) {
        return "application/json";
    }
    if (path.rfind(".png") != std::string::npos) {
        return "image/png";
    }
    if (path.rfind(".svg") != std::string::npos) {
        return "image/svg+xml";
    }
    return "text/plain";
}

esp_err_t handle_static(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    std::string path = "/spiffs";
    if (std::strcmp(req->uri, "/") == 0) {
        path += "/index.html";
    } else {
        path += req->uri;
    }

    FILE *file = fopen(path.c_str(), "rb");
    if (!file) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_FAIL;
    }

    apply_cors(req, *ctx->config);
    httpd_resp_set_type(req, mime_for_path(path));

    std::vector<char> buffer(1024);
    size_t read = 0;
    while ((read = fread(buffer.data(), 1, buffer.size(), file)) > 0) {
        esp_err_t err = httpd_resp_send_chunk(req, buffer.data(), read);
        if (err != ESP_OK) {
            fclose(file);
            return err;
        }
    }
    fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t handle_options(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    apply_cors(req, *ctx->config);
    return httpd_resp_send(req, nullptr, 0);
}

} // namespace

esp_err_t start_http_server(HttpServerHandle &server, SystemConfig &config, TinyBmsBridge &bridge) {
    if (server.handle) {
        return ESP_OK;
    }

    httpd_config_t httpd_config = HTTPD_DEFAULT_CONFIG();
    httpd_config.server_port = 80;
    httpd_config.lru_purge_enable = true;
    httpd_config.uri_match_fn = httpd_uri_match_wildcard;
    httpd_config.max_uri_handlers = 16;

    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &httpd_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    auto *ctx = new ServerContext{&config, &bridge};

    httpd_uri_t status_uri{
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handle_status,
        .user_ctx = ctx,
    };

    httpd_uri_t config_get_uri{
        .uri = "/api/config/system",
        .method = HTTP_GET,
        .handler = handle_config_get,
        .user_ctx = ctx,
    };

    httpd_uri_t config_post_uri{
        .uri = "/api/config/system",
        .method = HTTP_POST,
        .handler = handle_config_post,
        .user_ctx = ctx,
    };

    httpd_uri_t options_uri{
        .uri = "/*",
        .method = HTTP_OPTIONS,
        .handler = handle_options,
        .user_ctx = ctx,
    };

    httpd_uri_t static_uri{
        .uri = "/*",
        .method = HTTP_GET,
        .handler = handle_static,
        .user_ctx = ctx,
    };

    httpd_register_uri_handler(handle, &status_uri);
    httpd_register_uri_handler(handle, &config_get_uri);
    httpd_register_uri_handler(handle, &config_post_uri);
    httpd_register_uri_handler(handle, &options_uri);
    httpd_register_uri_handler(handle, &static_uri);

    server.handle = handle;
    server.ctx = ctx;
    ESP_LOGI(TAG, "HTTP server listening on port %d", httpd_config.server_port);
    return ESP_OK;
}

void stop_http_server(HttpServerHandle &server) {
    if (!server.handle) {
        return;
    }
    httpd_handle_t handle = static_cast<httpd_handle_t>(server.handle);
    httpd_stop(handle);
    delete static_cast<ServerContext *>(server.ctx);
    server.handle = nullptr;
    server.ctx = nullptr;
}

} // namespace tinybms
