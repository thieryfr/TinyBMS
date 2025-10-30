#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <cstdint>

namespace tinybms::diagnostics {

struct BridgeHealth {
    uint64_t last_uart_byte_us = 0;
    uint64_t last_can_publish_us = 0;
    uint32_t parsed_samples = 0;
    uint32_t dropped_samples = 0;
    uint32_t can_errors = 0;
    SemaphoreHandle_t lock = nullptr;
};

struct BridgeHealthSnapshot {
    uint64_t last_uart_delta_ms = 0;
    uint64_t last_can_delta_ms = 0;
    uint32_t parsed_samples = 0;
    uint32_t dropped_samples = 0;
    uint32_t can_errors = 0;
};

esp_err_t init(BridgeHealth &health);
void destroy(BridgeHealth &health);
void note_uart_activity(BridgeHealth &health);
void note_parsed_sample(BridgeHealth &health);
void note_dropped_sample(BridgeHealth &health);
void note_can_publish(BridgeHealth &health);
void note_can_error(BridgeHealth &health, esp_err_t err);
void log_snapshot(const BridgeHealth &health, const char *tag);
BridgeHealthSnapshot snapshot(const BridgeHealth &health);

} // namespace tinybms::diagnostics
