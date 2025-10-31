/**
 * @file esp32_uart_idf.cpp
 * @brief ESP-IDF native UART HAL for PlatformIO
 *
 * Phase 2: Migration Périphériques
 * Uses native ESP-IDF uart_driver API while compatible with PlatformIO
 */

#include "hal/interfaces/ihal_uart.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <memory>
#include <cstring>

namespace hal {

static const char* TAG = "ESP32UartIDF";

class ESP32UartIDF : public IHalUart {
public:
    ESP32UartIDF()
        : uart_num_(UART_NUM_MAX)
        , timeout_ms_(1000)
        , initialized_(false) {
        memset(&last_config_, 0, sizeof(last_config_));
    }

    ~ESP32UartIDF() override {
        if (initialized_) {
            uart_driver_delete(uart_num_);
        }
    }

    Status initialize(const UartConfig& config) override {
        if (config.rx_pin < 0 || config.tx_pin < 0) {
            ESP_LOGE(TAG, "Invalid UART pin configuration");
            return Status::Error;
        }

        // Check if already initialized with same config (idempotent)
        if (initialized_) {
            bool config_changed = (last_config_.rx_pin != config.rx_pin ||
                                  last_config_.tx_pin != config.tx_pin ||
                                  last_config_.baudrate != config.baudrate ||
                                  last_config_.timeout_ms != config.timeout_ms);

            if (!config_changed) {
                ESP_LOGD(TAG, "UART already initialized with same config, skipping");
                return Status::Ok;
            }

            // Config changed, need to reinitialize
            ESP_LOGI(TAG, "UART config changed, reinitializing...");
            uart_driver_delete(uart_num_);
            initialized_ = false;
        }

        uart_num_ = UART_NUM_2;  // Use UART2 for TinyBMS
        timeout_ms_ = config.timeout_ms;
        last_config_ = config;  // Store config for comparison

        uart_config_t uart_config = {
            .baud_rate = static_cast<int>(config.baudrate),
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT,
        };

        esp_err_t err = uart_param_config(uart_num_, &uart_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(err));
            return Status::Error;
        }

        err = uart_set_pin(uart_num_,
                          config.tx_pin,
                          config.rx_pin,
                          UART_PIN_NO_CHANGE,
                          UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(err));
            return Status::Error;
        }

        const int rx_buffer_size = 2048;
        const int tx_buffer_size = 1024;

        err = uart_driver_install(uart_num_,
                                  rx_buffer_size,
                                  tx_buffer_size,
                                  0,
                                  NULL,
                                  0);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(err));
            return Status::Error;
        }

        initialized_ = true;
        ESP_LOGI(TAG, "UART%d initialized: RX=%d, TX=%d, baud=%lu",
                 uart_num_, config.rx_pin, config.tx_pin, config.baudrate);

        return Status::Ok;
    }

    void setTimeout(uint32_t timeout_ms) override {
        timeout_ms_ = timeout_ms;
    }

    uint32_t getTimeout() const override {
        return timeout_ms_;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        if (!initialized_ || buffer == nullptr || size == 0) {
            return 0;
        }

        int written = uart_write_bytes(uart_num_, buffer, size);
        return (written < 0) ? 0 : static_cast<size_t>(written);
    }

    void flush() override {
        if (!initialized_) {
            return;
        }

        uart_wait_tx_done(uart_num_, pdMS_TO_TICKS(timeout_ms_));
    }

    size_t readBytes(uint8_t* buffer, size_t length) override {
        if (!initialized_ || buffer == nullptr || length == 0) {
            return 0;
        }

        int read = uart_read_bytes(uart_num_,
                                   buffer,
                                   length,
                                   pdMS_TO_TICKS(timeout_ms_));

        return (read < 0) ? 0 : static_cast<size_t>(read);
    }

    int available() override {
        if (!initialized_) {
            return 0;
        }

        size_t available = 0;
        uart_get_buffered_data_len(uart_num_, &available);
        return static_cast<int>(available);
    }

    int read() override {
        if (!initialized_) {
            return -1;
        }

        uint8_t byte;
        int len = uart_read_bytes(uart_num_, &byte, 1, 0);
        return (len > 0) ? byte : -1;
    }

private:
    uart_port_t uart_num_;
    uint32_t timeout_ms_;
    bool initialized_;
    UartConfig last_config_;  // Store last config for idempotent reinit
};

std::unique_ptr<IHalUart> createEsp32IdfUart() {
    return std::make_unique<ESP32UartIDF>();
}

} // namespace hal
