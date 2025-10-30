#include "wifi_manager.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include <algorithm>
#include <cstring>

namespace tinybms {
namespace {
constexpr const char *TAG = "wifi";
bool wifi_initialised = false;
bool wifi_started = false;
esp_netif_t *sta_netif = nullptr;
esp_netif_t *ap_netif = nullptr;

esp_err_t apply_wifi_config(const SystemConfig &config) {
    wifi_mode_t mode = config.sta.enabled ? WIFI_MODE_APSTA : WIFI_MODE_AP;
    esp_err_t err = esp_wifi_set_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode: %s", esp_err_to_name(err));
        return err;
    }

    wifi_config_t ap_config = {};
    std::memset(&ap_config, 0, sizeof(ap_config));
    std::strncpy(reinterpret_cast<char *>(ap_config.ap.ssid), config.ap.ssid.c_str(), sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = std::min<size_t>(config.ap.ssid.length(), sizeof(ap_config.ap.ssid));
    ap_config.ap.channel = config.ap.channel;
    ap_config.ap.max_connection = config.ap.max_connections;
    if (config.ap.password.length() < 8) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
        ap_config.ap.password[0] = '\0';
    } else {
        std::strncpy(reinterpret_cast<char *>(ap_config.ap.password), config.ap.password.c_str(), sizeof(ap_config.ap.password));
        ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }

    err = esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return err;
    }

    if (config.sta.enabled) {
        wifi_config_t sta_config = {};
        std::memset(&sta_config, 0, sizeof(sta_config));
        std::strncpy(reinterpret_cast<char *>(sta_config.sta.ssid), config.sta.ssid.c_str(), sizeof(sta_config.sta.ssid));
        std::strncpy(reinterpret_cast<char *>(sta_config.sta.password), config.sta.password.c_str(), sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_config.sta.pmf_cfg.capable = true;
        sta_config.sta.pmf_cfg.required = false;

        err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

esp_err_t ensure_wifi_initialised() {
    if (wifi_initialised) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to init netif: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(err));
        return err;
    }

    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set Wi-Fi storage: %s", esp_err_to_name(err));
        return err;
    }

    wifi_initialised = true;
    return ESP_OK;
}

} // namespace

esp_err_t wifi_manager_start(const SystemConfig &config) {
    esp_err_t err = ensure_wifi_initialised();
    if (err != ESP_OK) {
        return err;
    }

    err = apply_wifi_config(config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    if (config.sta.enabled) {
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to connect STA: %s", esp_err_to_name(err));
        }
    }

    wifi_started = true;
    ESP_LOGI(TAG, "Wi-Fi started (mode=%s)", config.sta.enabled ? "AP+STA" : "AP");
    return ESP_OK;
}

esp_err_t wifi_manager_update(const SystemConfig &config) {
    if (!wifi_started) {
        return wifi_manager_start(config);
    }

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    err = apply_wifi_config(config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restart Wi-Fi: %s", esp_err_to_name(err));
        return err;
    }

    if (config.sta.enabled) {
        err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to reconnect STA: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Wi-Fi configuration updated");
    return ESP_OK;
}

} // namespace tinybms
