#include "http_server.hpp"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "logger.hpp"
#include "system_config.hpp"
#include "wifi_manager.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace tinybms {
namespace {
constexpr const char *TAG = "http";
constexpr uint32_t kMinWsIntervalMs = 100;
constexpr uint32_t kMaxWsIntervalMs = 10000;
constexpr size_t kDefaultLogLimit = 64;

struct ServerContext {
    SystemConfig *config;
    TinyBmsBridge *bridge;
    httpd_handle_t handle;
    SemaphoreHandle_t ws_lock;
    std::vector<int> ws_clients;
    TaskHandle_t ws_task;
    bool ws_task_running;
};

ServerContext *g_server = nullptr;

void apply_cors(httpd_req_t *req, const SystemConfig &config) {
    if (!config.web.enable_cors) {
        return;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", config.web.cors_origin.c_str());
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type,Authorization");
}

std::string render_json(cJSON *root) {
    char *rendered = cJSON_PrintUnformatted(root);
    if (!rendered) {
        return {};
    }
    std::string payload(rendered);
    cJSON_free(rendered);
    return payload;
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
    cJSON_AddNumberToObject(web, "websocket_interval_ms", config.web.websocket_update_interval_ms);
    cJSON_AddNumberToObject(web, "max_ws_clients", config.web.max_ws_clients);
    cJSON_AddBoolToObject(web, "enable_auth", config.web.enable_auth);
    cJSON_AddStringToObject(web, "username", config.web.username.c_str());
    cJSON_AddStringToObject(web, "password", config.web.password.c_str());

    cJSON *logging = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "logging", logging);
    std::string level = log::level_to_string(config.logging.level);
    cJSON_AddStringToObject(logging, "level", level.c_str());
    cJSON_AddBoolToObject(logging, "web_enabled", config.logging.web_enabled);
    cJSON_AddBoolToObject(logging, "serial_enabled", config.logging.serial_enabled);

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
        const cJSON *interval = cJSON_GetObjectItemCaseSensitive(web, "websocket_interval_ms");
        if (cJSON_IsNumber(interval)) {
            uint32_t value = static_cast<uint32_t>(interval->valuedouble);
            value = std::clamp(value, kMinWsIntervalMs, kMaxWsIntervalMs);
            config.web.websocket_update_interval_ms = value;
        }
        const cJSON *clients = cJSON_GetObjectItemCaseSensitive(web, "max_ws_clients");
        if (cJSON_IsNumber(clients)) {
            uint8_t count = static_cast<uint8_t>(clients->valuedouble);
            if (count >= 1 && count <= 10) {
                config.web.max_ws_clients = count;
            }
        }
        const cJSON *auth = cJSON_GetObjectItemCaseSensitive(web, "enable_auth");
        if (cJSON_IsBool(auth)) {
            config.web.enable_auth = cJSON_IsTrue(auth);
        }
        const cJSON *user = cJSON_GetObjectItemCaseSensitive(web, "username");
        if (cJSON_IsString(user) && user->valuestring) {
            config.web.username = user->valuestring;
        }
        const cJSON *pwd = cJSON_GetObjectItemCaseSensitive(web, "password");
        if (cJSON_IsString(pwd) && pwd->valuestring) {
            config.web.password = pwd->valuestring;
        }
    }

    const cJSON *logging = cJSON_GetObjectItemCaseSensitive(root, "logging");
    if (cJSON_IsObject(logging)) {
        const cJSON *level = cJSON_GetObjectItemCaseSensitive(logging, "level");
        if (cJSON_IsString(level) && level->valuestring) {
            config.logging.level = log::level_from_string(level->valuestring);
        }
        const cJSON *web_enabled = cJSON_GetObjectItemCaseSensitive(logging, "web_enabled");
        if (cJSON_IsBool(web_enabled)) {
            config.logging.web_enabled = cJSON_IsTrue(web_enabled);
        }
        const cJSON *serial_enabled = cJSON_GetObjectItemCaseSensitive(logging, "serial_enabled");
        if (cJSON_IsBool(serial_enabled)) {
            config.logging.serial_enabled = cJSON_IsTrue(serial_enabled);
        }
    }

    return true;
}

cJSON *build_status_json(ServerContext &ctx) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000ULL);

    auto health = ctx.bridge->health_snapshot();
    cJSON *diag = cJSON_CreateObject();
    cJSON_AddItemToObject(root, "diagnostics", diag);
    cJSON_AddNumberToObject(diag, "last_uart_delta_ms", health.last_uart_delta_ms);
    cJSON_AddNumberToObject(diag, "last_can_delta_ms", health.last_can_delta_ms);
    cJSON_AddNumberToObject(diag, "parsed_samples", health.parsed_samples);
    cJSON_AddNumberToObject(diag, "dropped_samples", health.dropped_samples);
    cJSON_AddNumberToObject(diag, "can_errors", health.can_errors);

    MeasurementSample sample{};
    if (ctx.bridge->latest_sample(sample)) {
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
    return root;
}

esp_err_t respond_with_json(httpd_req_t *req, cJSON *root, const SystemConfig &config) {
    std::string payload = render_json(root);
    if (payload.empty()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json alloc failed");
        return ESP_FAIL;
    }
    apply_cors(req, config);
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, payload.c_str(), HTTPD_RESP_USE_STRLEN);
}

void remove_ws_client(ServerContext &ctx, int fd) {
    if (!ctx.ws_lock) {
        return;
    }
    if (xSemaphoreTake(ctx.ws_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }
    ctx.ws_clients.erase(std::remove(ctx.ws_clients.begin(), ctx.ws_clients.end(), fd), ctx.ws_clients.end());
    xSemaphoreGive(ctx.ws_lock);
}

void notify_ws_task(ServerContext &ctx) {
    if (ctx.ws_task) {
        xTaskNotifyGive(ctx.ws_task);
    }
}

void websocket_task(void *arg) {
    auto *ctx = static_cast<ServerContext *>(arg);
    while (ctx->ws_task_running) {
        if (ctx->ws_lock) {
            bool has_clients = false;
            if (xSemaphoreTake(ctx->ws_lock, pdMS_TO_TICKS(10)) == pdTRUE) {
                has_clients = !ctx->ws_clients.empty();
                xSemaphoreGive(ctx->ws_lock);
            }
            if (has_clients) {
                cJSON *root = build_status_json(*ctx);
                std::string payload = render_json(root);
                cJSON_Delete(root);
                if (!payload.empty()) {
                    if (xSemaphoreTake(ctx->ws_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
                        std::vector<int> failed;
                        for (int fd : ctx->ws_clients) {
                            httpd_ws_frame_t frame{};
                            frame.type = HTTPD_WS_TYPE_TEXT;
                            frame.final = true;
                            frame.payload = reinterpret_cast<uint8_t *>(const_cast<char *>(payload.c_str()));
                            frame.len = payload.size();
                            esp_err_t err = httpd_ws_send_frame_async(ctx->handle, fd, &frame);
                            if (err != ESP_OK) {
                                failed.push_back(fd);
                            }
                        }
                        for (int fd : failed) {
                            ctx->ws_clients.erase(std::remove(ctx->ws_clients.begin(), ctx->ws_clients.end(), fd),
                                                   ctx->ws_clients.end());
                        }
                        xSemaphoreGive(ctx->ws_lock);
                    }
                }
            }
        }
        uint32_t interval = std::clamp(ctx->config->web.websocket_update_interval_ms, kMinWsIntervalMs, kMaxWsIntervalMs);
        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(interval)) > 0) {
            continue;
        }
    }
    if (ctx->ws_lock && xSemaphoreTake(ctx->ws_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        ctx->ws_clients.clear();
        xSemaphoreGive(ctx->ws_lock);
    }
    ctx->ws_task = nullptr;
    vTaskDelete(nullptr);
}

void start_ws_task(ServerContext &ctx) {
    if (!ctx.config->web.enable_websocket) {
        return;
    }
    if (!ctx.ws_lock) {
        ctx.ws_lock = xSemaphoreCreateMutex();
    }
    if (ctx.ws_task) {
        notify_ws_task(ctx);
        return;
    }
    ctx.ws_task_running = true;
    BaseType_t created = xTaskCreate(websocket_task, "ws_status", 4096, &ctx, 5, &ctx.ws_task);
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Failed to start WebSocket task");
        ctx.ws_task = nullptr;
        ctx.ws_task_running = false;
    }
}

void stop_ws_task(ServerContext &ctx) {
    if (!ctx.ws_task) {
        if (ctx.ws_lock && xSemaphoreTake(ctx.ws_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            ctx.ws_clients.clear();
            xSemaphoreGive(ctx.ws_lock);
        }
        return;
    }
    ctx.ws_task_running = false;
    notify_ws_task(ctx);
    while (ctx.ws_task) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (ctx.ws_lock && xSemaphoreTake(ctx.ws_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
        ctx.ws_clients.clear();
        xSemaphoreGive(ctx.ws_lock);
    }
}

void refresh_ws_task(ServerContext &ctx) {
    if (!ctx.config->web.enable_websocket) {
        stop_ws_task(ctx);
        return;
    }
    start_ws_task(ctx);
}

esp_err_t websocket_close_handler(httpd_handle_t, int sockfd) {
    if (!g_server) {
        return ESP_OK;
    }
    remove_ws_client(*g_server, sockfd);
    return ESP_OK;
}

esp_err_t handle_status(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    cJSON *root = build_status_json(*ctx);
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
    std::vector<char> body(req->content_len + 1, 0);
    size_t received = 0;
    while (received < static_cast<size_t>(req->content_len)) {
        int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

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

    err = log::set_global_level(new_config.logging.level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update log level: %s", esp_err_to_name(err));
    }

    *ctx->config = new_config;
    refresh_ws_task(*ctx);

    cJSON *response = system_config_to_json(*ctx->config);
    err = respond_with_json(req, response, *ctx->config);
    cJSON_Delete(response);
    return err;
}

esp_err_t handle_logs_recent(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    auto entries = log::recent(kDefaultLogLimit);
    cJSON *root = cJSON_CreateObject();
    cJSON *array = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "entries", array);

    for (const auto &entry : entries) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "timestamp_ms", entry.timestamp_ms);
        cJSON_AddStringToObject(item, "level", log::level_to_string(entry.level).c_str());
        cJSON_AddStringToObject(item, "tag", entry.tag.c_str());
        cJSON_AddStringToObject(item, "message", entry.message.c_str());
        cJSON_AddItemToArray(array, item);
    }

    esp_err_t err = respond_with_json(req, root, *ctx->config);
    cJSON_Delete(root);
    return err;
}

esp_err_t handle_logs_level(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    std::vector<char> body(req->content_len + 1, 0);
    size_t received = 0;
    while (received < static_cast<size_t>(req->content_len)) {
        int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    cJSON *root = cJSON_Parse(body.data());
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid json");
        return ESP_FAIL;
    }

    const cJSON *level = cJSON_GetObjectItemCaseSensitive(root, "level");
    if (!cJSON_IsString(level) || !level->valuestring) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing level");
        return ESP_FAIL;
    }

    SystemConfig new_config = *ctx->config;
    new_config.logging.level = log::level_from_string(level->valuestring);
    cJSON_Delete(root);

    esp_err_t err = save_system_config(new_config);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "persist failed");
        return err;
    }

    err = log::set_global_level(new_config.logging.level);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to update log level: %s", esp_err_to_name(err));
    }

    *ctx->config = new_config;
    refresh_ws_task(*ctx);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "level", log::level_to_string(ctx->config->logging.level).c_str());
    err = respond_with_json(req, response, *ctx->config);
    cJSON_Delete(response);
    return err;
}

esp_err_t handle_hardware_led(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    std::vector<char> body(req->content_len + 1, 0);
    size_t received = 0;
    while (received < static_cast<size_t>(req->content_len)) {
        int ret = httpd_req_recv(req, body.data() + received, req->content_len - received);
        if (ret <= 0) {
            return ESP_FAIL;
        }
        received += ret;
    }

    uint32_t duration_ms = 500;
    if (req->content_len > 0) {
        cJSON *root = cJSON_Parse(body.data());
        if (root) {
            const cJSON *duration = cJSON_GetObjectItemCaseSensitive(root, "duration_ms");
            if (cJSON_IsNumber(duration)) {
                duration_ms = static_cast<uint32_t>(duration->valuedouble);
            }
            cJSON_Delete(root);
        }
    }

    esp_err_t err = ctx->bridge->pulse_status_led(duration_ms);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "test failed");
        return err;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", true);
    err = respond_with_json(req, root, *ctx->config);
    cJSON_Delete(root);
    return err;
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
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    const char *mime = "text/plain";
    if (path.rfind(".html") != std::string::npos) {
        mime = "text/html";
    } else if (path.rfind(".css") != std::string::npos) {
        mime = "text/css";
    } else if (path.rfind(".js") != std::string::npos) {
        mime = "application/javascript";
    } else if (path.rfind(".json") != std::string::npos) {
        mime = "application/json";
    } else if (path.rfind(".png") != std::string::npos) {
        mime = "image/png";
    } else if (path.rfind(".svg") != std::string::npos) {
        mime = "image/svg+xml";
    }
    httpd_resp_set_type(req, mime);

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

esp_err_t handle_ws(httpd_req_t *req) {
    auto *ctx = static_cast<ServerContext *>(req->user_ctx);
    if (!ctx->config->web.enable_websocket) {
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "websocket disabled");
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        if (!ctx->ws_lock) {
            ctx->ws_lock = xSemaphoreCreateMutex();
        }
        int fd = httpd_req_to_sockfd(req);
        if (xSemaphoreTake(ctx->ws_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            bool already = std::find(ctx->ws_clients.begin(), ctx->ws_clients.end(), fd) != ctx->ws_clients.end();
            if (!already) {
                if (ctx->ws_clients.size() >= ctx->config->web.max_ws_clients) {
                    xSemaphoreGive(ctx->ws_lock);
                    httpd_resp_send_err(req, HTTPD_503_SERVICE_UNAVAILABLE, "too many clients");
                    return ESP_FAIL;
                }
                ctx->ws_clients.push_back(fd);
            }
            xSemaphoreGive(ctx->ws_lock);
        }
        start_ws_task(*ctx);
        return ESP_OK;
    }

    httpd_ws_frame_t frame{};
    frame.payload = nullptr;
    frame.len = 0;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }
    if (frame.len > 0) {
        std::vector<uint8_t> buffer(frame.len + 1, 0);
        frame.payload = buffer.data();
        err = httpd_ws_recv_frame(req, &frame, frame.len);
    }
    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        int fd = httpd_req_to_sockfd(req);
        remove_ws_client(*ctx, fd);
    }
    return err;
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
    httpd_config.max_uri_handlers = 20;

    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &httpd_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    auto *ctx = new ServerContext{&config, &bridge, handle, nullptr, {}, nullptr, false};
    g_server = ctx;

    httpd_register_close_fn(handle, websocket_close_handler);

    httpd_uri_t status_uri{};
    status_uri.uri = "/api/status";
    status_uri.method = HTTP_GET;
    status_uri.handler = handle_status;
    status_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &status_uri);

    httpd_uri_t config_get_uri{};
    config_get_uri.uri = "/api/config/system";
    config_get_uri.method = HTTP_GET;
    config_get_uri.handler = handle_config_get;
    config_get_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &config_get_uri);

    httpd_uri_t config_post_uri{};
    config_post_uri.uri = "/api/config/system";
    config_post_uri.method = HTTP_POST;
    config_post_uri.handler = handle_config_post;
    config_post_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &config_post_uri);

    httpd_uri_t logs_uri{};
    logs_uri.uri = "/api/logs/recent";
    logs_uri.method = HTTP_GET;
    logs_uri.handler = handle_logs_recent;
    logs_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &logs_uri);

    httpd_uri_t logs_level_uri{};
    logs_level_uri.uri = "/api/logs/level";
    logs_level_uri.method = HTTP_POST;
    logs_level_uri.handler = handle_logs_level;
    logs_level_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &logs_level_uri);

    httpd_uri_t hardware_led_uri{};
    hardware_led_uri.uri = "/api/hardware/test/status-led";
    hardware_led_uri.method = HTTP_POST;
    hardware_led_uri.handler = handle_hardware_led;
    hardware_led_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &hardware_led_uri);

    httpd_uri_t options_uri{};
    options_uri.uri = "/*";
    options_uri.method = HTTP_OPTIONS;
    options_uri.handler = handle_options;
    options_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &options_uri);

    httpd_uri_t ws_uri{};
    ws_uri.uri = "/ws/status";
    ws_uri.method = HTTP_GET;
    ws_uri.handler = handle_ws;
    ws_uri.user_ctx = ctx;
    ws_uri.is_websocket = true;
    httpd_register_uri_handler(handle, &ws_uri);

    httpd_uri_t static_uri{};
    static_uri.uri = "/*";
    static_uri.method = HTTP_GET;
    static_uri.handler = handle_static;
    static_uri.user_ctx = ctx;
    httpd_register_uri_handler(handle, &static_uri);

    server.handle = handle;
    server.ctx = ctx;

    refresh_ws_task(*ctx);

    ESP_LOGI(TAG, "HTTP server listening on port %d", httpd_config.server_port);
    return ESP_OK;
}

void stop_http_server(HttpServerHandle &server) {
    if (!server.handle) {
        return;
    }
    auto *ctx = static_cast<ServerContext *>(server.ctx);
    stop_ws_task(*ctx);
    httpd_stop(static_cast<httpd_handle_t>(server.handle));
    if (ctx->ws_lock) {
        vSemaphoreDelete(ctx->ws_lock);
    }
    delete ctx;
    g_server = nullptr;
    server.handle = nullptr;
    server.ctx = nullptr;
}

} // namespace tinybms
