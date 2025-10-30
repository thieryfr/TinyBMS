#include "system_config.hpp"

#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

namespace tinybms {
namespace {
constexpr const char *TAG = "system-config";
constexpr const char *NVS_NAMESPACE = "tinybms";

std::string get_string(nvs_handle_t handle, const char *key, const char *fallback) {
    size_t required = 0;
    esp_err_t err = nvs_get_str(handle, key, nullptr, &required);
    if (err == ESP_ERR_NVS_NOT_FOUND || required == 0) {
        return fallback ? fallback : "";
    }
    std::string value;
    value.resize(required);
    err = nvs_get_str(handle, key, value.data(), &required);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read key %s: %s", key, esp_err_to_name(err));
        return fallback ? fallback : "";
    }
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

void set_string(nvs_handle_t handle, const char *key, const std::string &value) {
    esp_err_t err = nvs_set_str(handle, key, value.c_str());
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store key %s: %s", key, esp_err_to_name(err));
    }
}

} // namespace

esp_err_t load_system_config(SystemConfig &config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // use defaults
        config.device_name = "tinybms";
        config.ap.ssid = "TinyBMS";
        config.ap.password = "tinybms";
        config.ap.channel = 6;
        config.ap.max_connections = 4;
        config.sta.enabled = false;
        config.sta.ssid.clear();
        config.sta.password.clear();
        config.web.enable_cors = true;
        config.web.enable_websocket = true;
        config.web.cors_origin = "*";
        return ESP_OK;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    config.device_name = get_string(handle, "device_name", "tinybms");
    config.ap.ssid = get_string(handle, "ap_ssid", "TinyBMS");
    config.ap.password = get_string(handle, "ap_pwd", "tinybms");

    uint8_t channel = 0;
    err = nvs_get_u8(handle, "ap_channel", &channel);
    config.ap.channel = (err == ESP_OK && channel >= 1 && channel <= 13) ? channel : 6;

    uint8_t max_conn = 0;
    err = nvs_get_u8(handle, "ap_max_conn", &max_conn);
    config.ap.max_connections = (err == ESP_OK && max_conn > 0 && max_conn <= 10) ? max_conn : 4;

    uint8_t sta_enabled = 0;
    err = nvs_get_u8(handle, "sta_enabled", &sta_enabled);
    config.sta.enabled = (err == ESP_OK) ? (sta_enabled != 0) : false;
    config.sta.ssid = get_string(handle, "sta_ssid", "");
    config.sta.password = get_string(handle, "sta_pwd", "");

    uint8_t ws_enabled = 1;
    err = nvs_get_u8(handle, "ws_enabled", &ws_enabled);
    config.web.enable_websocket = (err == ESP_OK) ? (ws_enabled != 0) : true;

    uint8_t cors_enabled = 1;
    err = nvs_get_u8(handle, "cors_enabled", &cors_enabled);
    config.web.enable_cors = (err == ESP_OK) ? (cors_enabled != 0) : true;
    config.web.cors_origin = get_string(handle, "cors_origin", "*");

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t save_system_config(const SystemConfig &config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for write: %s", esp_err_to_name(err));
        return err;
    }

    set_string(handle, "device_name", config.device_name);
    set_string(handle, "ap_ssid", config.ap.ssid);
    set_string(handle, "ap_pwd", config.ap.password);
    err = nvs_set_u8(handle, "ap_channel", config.ap.channel);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store AP channel: %s", esp_err_to_name(err));
    }
    err = nvs_set_u8(handle, "ap_max_conn", config.ap.max_connections);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store AP max connections: %s", esp_err_to_name(err));
    }

    err = nvs_set_u8(handle, "sta_enabled", config.sta.enabled ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store STA flag: %s", esp_err_to_name(err));
    }
    set_string(handle, "sta_ssid", config.sta.ssid);
    set_string(handle, "sta_pwd", config.sta.password);

    err = nvs_set_u8(handle, "ws_enabled", config.web.enable_websocket ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store websocket flag: %s", esp_err_to_name(err));
    }
    err = nvs_set_u8(handle, "cors_enabled", config.web.enable_cors ? 1 : 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to store CORS flag: %s", esp_err_to_name(err));
    }
    set_string(handle, "cors_origin", config.web.cors_origin);

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    nvs_close(handle);
    return err;
}

} // namespace tinybms
