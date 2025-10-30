/**
 * @file esp32_uart_idf.h
 * @brief ESP-IDF native UART HAL implementation header
 *
 * Phase 1: Fondations ESP-IDF
 */

#pragma once

#include "hal/interfaces/ihal_uart.h"
#include "driver/uart.h"

namespace hal {

class ESP32UartIDF : public IHalUart {
public:
    ESP32UartIDF();
    ~ESP32UartIDF() override;

    // IHalUart interface implementation
    Status initialize(const UartConfig& config) override;
    void setTimeout(uint32_t timeout_ms) override;
    uint32_t getTimeout() const override;
    size_t write(const uint8_t* buffer, size_t size) override;
    void flush() override;
    size_t readBytes(uint8_t* buffer, size_t length) override;
    int available() override;
    int read() override;

private:
    uart_port_t uart_num_;
    uint32_t timeout_ms_;
    bool initialized_;
};

} // namespace hal
