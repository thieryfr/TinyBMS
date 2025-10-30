/**
 * @file esp32_can_idf.cpp
 * @brief ESP-IDF native CAN (TWAI) HAL implementation
 *
 * Phase 1: Fondations ESP-IDF
 * Implements IHalCan using native ESP-IDF twai_driver API
 */

#include "esp32_can_idf.h"
#include "esp_log.h"
#include <cstring>

namespace hal {

static const char* TAG = "ESP32CanIDF";

ESP32CanIDF::ESP32CanIDF()
    : initialized_(false)
    , stats_{}
    , config_{} {
}

ESP32CanIDF::~ESP32CanIDF() {
    if (initialized_) {
        twai_stop();
        twai_driver_uninstall();
    }
}

twai_timing_config_t ESP32CanIDF::getBitrateConfig(uint32_t bitrate) {
    // Return TWAI timing config for common bitrates
    switch (bitrate) {
        case 25000:
            return TWAI_TIMING_CONFIG_25KBITS();
        case 50000:
            return TWAI_TIMING_CONFIG_50KBITS();
        case 100000:
            return TWAI_TIMING_CONFIG_100KBITS();
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
            ESP_LOGW(TAG, "Unsupported bitrate %lu, using 500kbps", bitrate);
            return TWAI_TIMING_CONFIG_500KBITS();
    }
}

Status ESP32CanIDF::initialize(const CanConfig& config) {
    // Validate configuration
    if (config.tx_pin < 0 || config.rx_pin < 0) {
        ESP_LOGE(TAG, "Invalid CAN pin configuration");
        return Status::InvalidArgument;
    }

    config_ = config;

    // General configuration
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
        static_cast<gpio_num_t>(config.tx_pin),
        static_cast<gpio_num_t>(config.rx_pin),
        TWAI_MODE_NORMAL
    );

    // Set TX queue length (for non-blocking sends)
    g_config.tx_queue_len = 10;
    g_config.rx_queue_len = 10;

    // Timing configuration (bitrate)
    twai_timing_config_t t_config = getBitrateConfig(config.bitrate);

    // Filter configuration
    // Start with accept-all, will configure specific filters if needed
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver
    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver install failed: %s", esp_err_to_name(err));
        return Status::Error;
    }

    // Start TWAI driver
    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return Status::Error;
    }

    initialized_ = true;
    resetStats();

    ESP_LOGI(TAG, "CAN initialized: TX=%d, RX=%d, bitrate=%lu",
             config.tx_pin, config.rx_pin, config.bitrate);

    return Status::Ok;
}

Status ESP32CanIDF::transmit(const CanFrame& frame) {
    if (!initialized_) {
        ESP_LOGW(TAG, "CAN not initialized");
        return Status::Error;
    }

    // Convert HAL CanFrame to TWAI message
    twai_message_t message = {};
    message.identifier = frame.id;
    message.data_length_code = frame.dlc;
    message.extd = frame.extended ? 1 : 0;
    message.rtr = 0;  // Data frame (not remote)
    message.ss = 0;   // Not single shot
    message.self = 0; // Not self-reception

    // Copy data
    std::memcpy(message.data, frame.data.data(), frame.dlc);

    // Transmit with timeout (10ms)
    esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(10));

    if (err == ESP_OK) {
        stats_.tx_success++;
        return Status::Ok;
    } else if (err == ESP_ERR_TIMEOUT) {
        stats_.tx_errors++;
        ESP_LOGW(TAG, "CAN TX timeout");
        return Status::Timeout;
    } else {
        stats_.tx_errors++;
        ESP_LOGW(TAG, "CAN TX failed: %s", esp_err_to_name(err));
        return Status::Error;
    }
}

Status ESP32CanIDF::receive(CanFrame& frame, uint32_t timeout_ms) {
    if (!initialized_) {
        return Status::Error;
    }

    twai_message_t message;
    esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(timeout_ms));

    if (err == ESP_OK) {
        // Convert TWAI message to HAL CanFrame
        frame.id = message.identifier;
        frame.dlc = message.data_length_code;
        frame.extended = message.extd;
        std::memcpy(frame.data.data(), message.data, message.data_length_code);

        stats_.rx_success++;
        return Status::Ok;
    } else if (err == ESP_ERR_TIMEOUT) {
        return Status::Timeout;
    } else {
        stats_.rx_errors++;
        return Status::Error;
    }
}

Status ESP32CanIDF::configureFilters(const std::vector<CanFilterConfig>& filters) {
    // Note: TWAI filters can only be configured before driver start
    // For now, return unsupported (would require driver restart)
    // This is a limitation we can document

    if (filters.empty()) {
        return Status::Ok;  // Accept-all is default
    }

    ESP_LOGW(TAG, "Filter reconfiguration requires driver restart (not implemented in Phase 1)");
    return Status::Unsupported;
}

CanStats ESP32CanIDF::getStats() const {
    return stats_;
}

void ESP32CanIDF::resetStats() {
    stats_ = CanStats{};
}

} // namespace hal
