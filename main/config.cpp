#include "config.hpp"
#include "sdkconfig.h"
#include "esp_log.h"

namespace tinybms {
namespace {
constexpr const char *TAG = "bridge-config";
constexpr uint16_t DEFAULT_MQTT_PORT = 1883;

twai_timing_config_t resolve_timing(uint32_t bitrate) {
    switch (bitrate) {
        case 125000:
            return TWAI_TIMING_CONFIG_125KBITS();
        case 250000:
            return TWAI_TIMING_CONFIG_250KBITS();
        case 500000:
            return TWAI_TIMING_CONFIG_500KBITS();
        case 800000:
            return TWAI_TIMING_CONFIG_800KBITS();
        case 1000000:
            return TWAI_TIMING_CONFIG_1MBITS();
        default:
            ESP_LOGW(TAG, "Unsupported bitrate %u, defaulting to 500 kbps", bitrate);
            return TWAI_TIMING_CONFIG_500KBITS();
    }
}

} // namespace

BridgeConfig load_bridge_config() {
    BridgeConfig cfg{};
    cfg.uart_port = static_cast<uart_port_t>(CONFIG_TINYBMS_UART_PORT);
    cfg.pins.uart_rx = static_cast<gpio_num_t>(CONFIG_TINYBMS_UART_RX_PIN);
    cfg.pins.uart_tx = static_cast<gpio_num_t>(CONFIG_TINYBMS_UART_TX_PIN);
    cfg.pins.can_rx = static_cast<gpio_num_t>(CONFIG_TINYBMS_CAN_RX_PIN);
    cfg.pins.can_tx = static_cast<gpio_num_t>(CONFIG_TINYBMS_CAN_TX_PIN);
    cfg.pins.status_led = static_cast<gpio_num_t>(CONFIG_TINYBMS_STATUS_LED_PIN);

    cfg.timings.uart_baudrate = CONFIG_TINYBMS_UART_BAUD;
    cfg.timings.sample_queue_length = CONFIG_TINYBMS_SAMPLE_QUEUE_LENGTH;
    cfg.timings.keepalive_period_ms = CONFIG_TINYBMS_KEEPALIVE_PERIOD_MS;
    cfg.timings.diagnostic_period_ms = CONFIG_TINYBMS_DIAGNOSTIC_PERIOD_MS;

    cfg.can_general = TWAI_GENERAL_CONFIG_DEFAULT(cfg.pins.can_tx, cfg.pins.can_rx, TWAI_MODE_NORMAL);
    cfg.can_general.tx_queue_len = 16;
    cfg.can_general.rx_queue_len = 64;
    cfg.can_general.alerts_enabled = TWAI_ALERT_TX_FAILED | TWAI_ALERT_RECOVERY_IN_PROGRESS |
                                     TWAI_ALERT_BUS_RECOVERED | TWAI_ALERT_ERR_PASS |
                                     TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_DATA;

    cfg.can_timing = resolve_timing(CONFIG_TINYBMS_CAN_BITRATE);
    cfg.can_filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    cfg.mqtt.enabled = CONFIG_TINYBMS_MQTT_ENABLED;
    cfg.mqtt.broker_host = CONFIG_TINYBMS_MQTT_BROKER;
    cfg.mqtt.topics.root = CONFIG_TINYBMS_MQTT_ROOT_TOPIC;
    cfg.mqtt.topics.telemetry = CONFIG_TINYBMS_MQTT_TELEMETRY_TOPIC;
    cfg.mqtt.topics.status = CONFIG_TINYBMS_MQTT_STATUS_TOPIC;

    if (cfg.mqtt.enabled && cfg.mqtt.broker_host.empty()) {
        ESP_LOGW(TAG, "MQTT enabled but broker hostname is empty, disabling module");
        cfg.mqtt.enabled = false;
    }

    int mqtt_port = CONFIG_TINYBMS_MQTT_PORT;
    if (mqtt_port < 1 || mqtt_port > 65535) {
        ESP_LOGW(TAG, "MQTT port %d out of range, defaulting to %u", mqtt_port, DEFAULT_MQTT_PORT);
        cfg.mqtt.port = DEFAULT_MQTT_PORT;
    } else {
        cfg.mqtt.port = static_cast<uint16_t>(mqtt_port);
    }

    if (cfg.mqtt.topics.root.empty()) {
        cfg.mqtt.topics.root = "tinybms";
        ESP_LOGW(TAG, "MQTT root topic missing, defaulting to %s", cfg.mqtt.topics.root.c_str());
    }

    if (cfg.mqtt.topics.telemetry.empty()) {
        cfg.mqtt.topics.telemetry = cfg.mqtt.topics.root + "/telemetry";
        ESP_LOGW(TAG, "MQTT telemetry topic missing, defaulting to %s", cfg.mqtt.topics.telemetry.c_str());
    }

    if (cfg.mqtt.topics.status.empty()) {
        cfg.mqtt.topics.status = cfg.mqtt.topics.root + "/status";
        ESP_LOGW(TAG, "MQTT status topic missing, defaulting to %s", cfg.mqtt.topics.status.c_str());
    }

    return cfg;
}

} // namespace tinybms
