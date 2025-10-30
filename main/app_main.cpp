#include "bridge.hpp"
#include "config.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

extern "C" void app_main(void) {
    constexpr const char *TAG = "tinybms-main";

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

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

    ESP_LOGI(TAG, "TinyBMS ↔ Victron bridge running (ESP-IDF)");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
