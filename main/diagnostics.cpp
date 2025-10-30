#include "diagnostics.hpp"
#include <functional>

namespace tinybms::diagnostics {
namespace {
constexpr const char *TAG = "bridge-diag";

void with_lock(const BridgeHealth &health, const std::function<void()> &fn) {
    if (!health.lock) {
        return;
    }
    if (xSemaphoreTake(health.lock, pdMS_TO_TICKS(50)) == pdTRUE) {
        fn();
        xSemaphoreGive(health.lock);
    }
}

} // namespace

esp_err_t init(BridgeHealth &health) {
    health.lock = xSemaphoreCreateMutex();
    if (!health.lock) {
        return ESP_ERR_NO_MEM;
    }
    health.last_uart_byte_us = esp_timer_get_time();
    health.last_can_publish_us = esp_timer_get_time();
    health.parsed_samples = 0;
    health.dropped_samples = 0;
    health.can_errors = 0;
    return ESP_OK;
}

void destroy(BridgeHealth &health) {
    if (health.lock) {
        vSemaphoreDelete(health.lock);
        health.lock = nullptr;
    }
}

void note_uart_activity(BridgeHealth &health) {
    with_lock(health, [&]() { health.last_uart_byte_us = esp_timer_get_time(); });
}

void note_parsed_sample(BridgeHealth &health) {
    with_lock(health, [&]() { health.parsed_samples++; });
}

void note_dropped_sample(BridgeHealth &health) {
    with_lock(health, [&]() { health.dropped_samples++; });
}

void note_can_publish(BridgeHealth &health) {
    with_lock(health, [&]() {
        health.last_can_publish_us = esp_timer_get_time();
    });
}

void note_can_error(BridgeHealth &health, esp_err_t) {
    with_lock(health, [&]() { health.can_errors++; });
}

void log_snapshot(const BridgeHealth &health, const char *tag) {
    with_lock(health, [&]() {
        uint64_t now = esp_timer_get_time();
        ESP_LOGI(tag ? tag : TAG,
                 "diag: last_uart=%llu ms, last_can=%llu ms, parsed=%u, dropped=%u, can_errors=%u",
                 (now - health.last_uart_byte_us) / 1000ULL,
                 (now - health.last_can_publish_us) / 1000ULL,
                 health.parsed_samples,
                 health.dropped_samples,
                 health.can_errors);
    });
}

} // namespace tinybms::diagnostics
