/**
 * @file esp32_idf_factory.h
 * @brief ESP32 IDF Factory for PlatformIO builds
 *
 * Phase 2: Migration Périphériques
 * This factory uses native ESP-IDF drivers while remaining compatible
 * with PlatformIO framework=arduino builds
 */

#pragma once

#include <memory>
#include "hal/hal_factory.h"

namespace hal {

/**
 * @brief Create ESP32 IDF-based HAL factory
 *
 * This factory creates HAL components using native ESP-IDF drivers:
 * - UART: driver/uart.h (UART2)
 * - CAN: driver/twai.h (TWAI)
 * - Storage: esp_spiffs.h + VFS
 * - GPIO: driver/gpio.h
 * - Timer: esp_timer.h
 * - Watchdog: esp_task_wdt.h
 *
 * Compatible with PlatformIO framework=arduino (ESP-IDF APIs accessible)
 */
std::unique_ptr<HalFactory> createEsp32IdfFactory();

} // namespace hal
