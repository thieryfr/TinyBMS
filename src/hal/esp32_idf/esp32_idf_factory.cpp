/**
 * @file esp32_idf_factory.cpp
 * @brief ESP32 IDF Factory implementation for PlatformIO
 *
 * Phase 2: Migration Périphériques
 * Creates HAL components using native ESP-IDF drivers
 */

#include "hal/esp32_idf_factory.h"
#include <memory>

namespace hal {

// Forward declarations of factory functions
std::unique_ptr<IHalUart> createEsp32IdfUart();
std::unique_ptr<IHalCan> createEsp32IdfCan();
std::unique_ptr<IHalStorage> createEsp32IdfStorage();
std::unique_ptr<IHalGpio> createEsp32IdfGpio();
std::unique_ptr<IHalTimer> createEsp32IdfTimer();
std::unique_ptr<IHalWatchdog> createEsp32IdfWatchdog();

class Esp32IdfHalFactory : public HalFactory {
public:
    std::unique_ptr<IHalUart> createUart() override {
        return createEsp32IdfUart();
    }

    std::unique_ptr<IHalCan> createCan() override {
        return createEsp32IdfCan();
    }

    std::unique_ptr<IHalStorage> createStorage() override {
        return createEsp32IdfStorage();
    }

    std::unique_ptr<IHalGpio> createGpio() override {
        return createEsp32IdfGpio();
    }

    std::unique_ptr<IHalTimer> createTimer() override {
        return createEsp32IdfTimer();
    }

    std::unique_ptr<IHalWatchdog> createWatchdog() override {
        return createEsp32IdfWatchdog();
    }
};

std::unique_ptr<HalFactory> createEsp32IdfFactory() {
    return std::make_unique<Esp32IdfHalFactory>();
}

} // namespace hal
