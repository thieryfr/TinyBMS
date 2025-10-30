#include "bridge.hpp"
#include "config.hpp"
#include "http_server.hpp"
#include "logger.hpp"
#include "system_config.hpp"
#include "wifi_manager.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_vfs_spiffs.h"

extern "C" void app_main(void) {
    constexpr const char *TAG = "tinybms-main";

    tinybms::log::init();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(loop_err);
    }

    static tinybms::SystemConfig system_config;
    ESP_ERROR_CHECK(tinybms::load_system_config(system_config));

    esp_err_t level_err = tinybms::log::set_global_level(system_config.logging.level);
    if (level_err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply log level: %s", esp_err_to_name(level_err));
    }

    tinybms::BridgeConfig config = tinybms::load_bridge_config();
    static tinybms::TinyBmsBridge bridge(config);

    err = bridge.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialise bridge: %s", esp_err_to_name(err));
        return;
    }

    err = bridge.start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start bridge: %s", esp_err_to_name(err));
        return;
    }

    err = tinybms::wifi_manager_start(system_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi: %s", esp_err_to_name(err));
    }

    esp_vfs_spiffs_conf_t spiffs_conf{
        .base_path = "/spiffs",
        .partition_label = "spiffs",
        .max_files = 16,
        .format_if_mount_failed = true,
    };

    err = esp_vfs_spiffs_register(&spiffs_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
    }

    static tinybms::HttpServerHandle http_server;
    err = tinybms::start_http_server(http_server, system_config, bridge);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "TinyBMS ↔ Victron bridge running (ESP-IDF)");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
