/**
 * @file esp32_can_idf.cpp
 * @brief ESP-IDF native CAN (TWAI) HAL for PlatformIO
 *
 * Phase 2: Migration Périphériques
 */

#include "hal/interfaces/ihal_can.h"
#include "driver/twai.h"
#include "esp_log.h"
#include <memory>
#include <cstring>

namespace hal {

static const char* TAG = "ESP32CanIDF";

class ESP32CanIDF : public IHalCan {
public:
    ESP32CanIDF() : initialized_(false), stats_{}, config_{} {}

    ~ESP32CanIDF() override { stopDriver(); }

    Status initialize(const CanConfig& config) override {
        if (config.tx_pin < 0 || config.rx_pin < 0) {
            ESP_LOGE(TAG, "Invalid CAN pin configuration");
            return Status::InvalidArgument;
        }

        if (initialized_) {
            bool config_changed = (config_.tx_pin != config.tx_pin ||
                                  config_.rx_pin != config.rx_pin ||
                                  config_.bitrate != config.bitrate ||
                                  !filtersEqual(config_.filters, config.filters));

            if (!config_changed) {
                ESP_LOGD(TAG, "CAN already initialized with same config, skipping");
                return Status::Ok;
            }

            ESP_LOGI(TAG, "CAN config changed, reinitializing...");
            stopDriver();
        }

        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(
            static_cast<gpio_num_t>(config.tx_pin),
            static_cast<gpio_num_t>(config.rx_pin),
            TWAI_MODE_NORMAL
        );
        g_config.tx_queue_len = 10;
        g_config.rx_queue_len = 10;

        twai_timing_config_t t_config = getTiming(config.bitrate);
        twai_filter_config_t f_config = buildFilterConfig(config.filters);

        esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI install failed: %s", esp_err_to_name(err));
            return Status::Error;
        }

        err = twai_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
            twai_driver_uninstall();
            return Status::Error;
        }

        initialized_ = true;
        config_ = config;
        resetStats();
        ESP_LOGI(TAG, "CAN initialized: TX=%d, RX=%d, bitrate=%lu",
                 config.tx_pin, config.rx_pin, config.bitrate);

        return Status::Ok;
    }

    Status transmit(const CanFrame& frame) override {
        if (!initialized_) {
            return Status::Error;
        }

        twai_message_t message = {};
        message.identifier = frame.id;
        message.data_length_code = frame.dlc;
        message.extd = frame.extended ? 1 : 0;
        std::memcpy(message.data, frame.data.data(), frame.dlc);

        esp_err_t err = twai_transmit(&message, pdMS_TO_TICKS(10));

        if (err == ESP_OK) {
            stats_.tx_success++;
            return Status::Ok;
        } else if (err == ESP_ERR_TIMEOUT) {
            stats_.tx_errors++;
            return Status::Timeout;
        } else {
            stats_.tx_errors++;
            return Status::Error;
        }
    }

    Status receive(CanFrame& frame, uint32_t timeout_ms) override {
        if (!initialized_) {
            return Status::Error;
        }

        twai_message_t message;
        esp_err_t err = twai_receive(&message, pdMS_TO_TICKS(timeout_ms));

        if (err == ESP_OK) {
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

    Status configureFilters(const std::vector<CanFilterConfig>& filters) override {
        CanConfig updated_config = config_;
        updated_config.filters = filters;
        return initialize(updated_config);
    }

    CanStats getStats() const override {
        return stats_;
    }

    void resetStats() override {
        stats_ = CanStats{};
    }

private:
    bool initialized_;
    CanStats stats_;
    CanConfig config_;

    void stopDriver() {
        if (!initialized_) {
            return;
        }

        esp_err_t err = twai_stop();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "TWAI stop failed: %s", esp_err_to_name(err));
        }

        err = twai_driver_uninstall();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "TWAI uninstall failed: %s", esp_err_to_name(err));
        }

        initialized_ = false;
    }

    bool filtersEqual(const std::vector<CanFilterConfig>& lhs,
                      const std::vector<CanFilterConfig>& rhs) const {
        if (lhs.size() != rhs.size()) {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i) {
            if (lhs[i].id != rhs[i].id ||
                lhs[i].mask != rhs[i].mask ||
                lhs[i].extended != rhs[i].extended) {
                return false;
            }
        }

        return true;
    }

    twai_filter_config_t buildFilterConfig(const std::vector<CanFilterConfig>& filters) {
        if (filters.empty()) {
            return TWAI_FILTER_CONFIG_ACCEPT_ALL();
        }

        twai_filter_config_t filter_config = {};
        filter_config.single_filter = true;

        const auto& filter = filters.front();
        if (filter.extended) {
            filter_config.acceptance_code = (filter.id & 0x1FFFFFFF) << 3;
            filter_config.acceptance_mask = (filter.mask & 0x1FFFFFFF) << 3;
        } else {
            filter_config.acceptance_code = (filter.id & 0x7FF) << 21;
            filter_config.acceptance_mask = (filter.mask & 0x7FF) << 21;
        }

        if (filters.size() > 1) {
            ESP_LOGW(TAG, "Multiple CAN filters requested, only the first one is applied");
        }

        return filter_config;
    }

    twai_timing_config_t getTiming(uint32_t bitrate) {
        switch (bitrate) {
            case 25000: return TWAI_TIMING_CONFIG_25KBITS();
            case 50000: return TWAI_TIMING_CONFIG_50KBITS();
            case 100000: return TWAI_TIMING_CONFIG_100KBITS();
            case 125000: return TWAI_TIMING_CONFIG_125KBITS();
            case 250000: return TWAI_TIMING_CONFIG_250KBITS();
            case 500000: return TWAI_TIMING_CONFIG_500KBITS();
            case 800000: return TWAI_TIMING_CONFIG_800KBITS();
            case 1000000: return TWAI_TIMING_CONFIG_1MBITS();
            default:
                ESP_LOGW(TAG, "Unsupported bitrate %lu, using 500k", bitrate);
                return TWAI_TIMING_CONFIG_500KBITS();
        }
    }
};

std::unique_ptr<IHalCan> createEsp32IdfCan() {
    return std::make_unique<ESP32CanIDF>();
}

} // namespace hal
