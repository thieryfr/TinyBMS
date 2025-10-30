/**
 * @file esp32_factory_idf.h
 * @brief ESP-IDF HAL Factory implementation header
 *
 * Phase 1: Fondations ESP-IDF
 * Factory for creating ESP-IDF native HAL instances
 */

#pragma once

#include "hal/hal_factory.h"

namespace hal {

class ESP32FactoryIDF : public HalFactory {
public:
    ESP32FactoryIDF() = default;
    ~ESP32FactoryIDF() override = default;

    std::unique_ptr<IHalUart> createUart() override;
    std::unique_ptr<IHalCan> createCan() override;
    std::unique_ptr<IHalStorage> createStorage() override;
    std::unique_ptr<IHalGpio> createGpio() override;
    std::unique_ptr<IHalTimer> createTimer() override;
    std::unique_ptr<IHalWatchdog> createWatchdog() override;
};

} // namespace hal
