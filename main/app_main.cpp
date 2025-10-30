#include "bridge.hpp"
#include "config.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

extern "C" void app_main(void) {
    constexpr const char *TAG = "tinybms-main";
    constexpr const char *MQTT_LWT_OFFLINE = "offline";

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

    static esp_mqtt_client_handle_t mqtt_client = nullptr;
    if (config.mqtt.enabled) {
        esp_mqtt_client_config_t mqtt_cfg = {};
        mqtt_cfg.broker.address.hostname = config.mqtt.broker_host.c_str();
        mqtt_cfg.broker.address.port = config.mqtt.port;
        mqtt_cfg.credentials.client_id = "tinybms-bridge";
        mqtt_cfg.session.last_will.topic = config.mqtt.topics.status.c_str();
        mqtt_cfg.session.last_will.msg = MQTT_LWT_OFFLINE;
        mqtt_cfg.session.last_will.qos = 0;
        mqtt_cfg.session.last_will.retain = true;

        ESP_LOGI(TAG, "Initialising MQTT bridge: %s:%u (telemetry=%s, status=%s)",
                 config.mqtt.broker_host.c_str(), static_cast<unsigned>(config.mqtt.port),
                 config.mqtt.topics.telemetry.c_str(), config.mqtt.topics.status.c_str());

        mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
        if (!mqtt_client) {
            ESP_LOGE(TAG, "Failed to create MQTT client");
        } else {
            err = esp_mqtt_client_start(mqtt_client);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
            } else {
                ESP_LOGI(TAG, "MQTT bridge started");
            }
        }
    } else {
        ESP_LOGI(TAG, "MQTT bridge disabled in configuration");
    }

    ESP_LOGI(TAG, "TinyBMS ↔ Victron bridge running (ESP-IDF)");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
