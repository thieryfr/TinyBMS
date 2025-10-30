/**
 * @file esp32_factory_idf.cpp
 * @brief ESP-IDF HAL Factory implementation
 *
 * Phase 1: Fondations ESP-IDF
 * Factory for creating ESP-IDF native HAL instances
 */

#include "esp32_factory_idf.h"
#include "esp32_uart_idf.h"
#include "esp32_can_idf.h"
#include "esp32_storage_idf.h"
#include "esp32_gpio_idf.h"
#include "esp32_timer_idf.h"
#include "esp32_watchdog_idf.h"

namespace hal {

std::unique_ptr<IHalUart> ESP32FactoryIDF::createUart() {
    return std::make_unique<ESP32UartIDF>();
}

std::unique_ptr<IHalCan> ESP32FactoryIDF::createCan() {
    return std::make_unique<ESP32CanIDF>();
}

std::unique_ptr<IHalStorage> ESP32FactoryIDF::createStorage() {
    return std::make_unique<ESP32StorageIDF>();
}

std::unique_ptr<IHalGpio> ESP32FactoryIDF::createGpio() {
    return std::make_unique<ESP32GpioIDF>();
}

std::unique_ptr<IHalTimer> ESP32FactoryIDF::createTimer() {
    return std::make_unique<ESP32TimerIDF>();
}

std::unique_ptr<IHalWatchdog> ESP32FactoryIDF::createWatchdog() {
    return std::make_unique<ESP32WatchdogIDF>();
}

} // namespace hal
