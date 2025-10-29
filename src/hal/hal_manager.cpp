#include "hal/hal_manager.h"

#include <stdexcept>

namespace hal {

HalManager& HalManager::instance() {
    static HalManager instance;
    return instance;
}

void HalManager::initialize(const HalConfig& config) {
    config_ = config;

    uart_ = factory().createUart();
    can_ = factory().createCan();
    storage_ = factory().createStorage();
    watchdog_ = factory().createWatchdog();

    if (!uart_ || !can_ || !storage_ || !watchdog_) {
        throw std::runtime_error("HAL factory produced null instance");
    }

    if (uart_->initialize(config_.uart) != Status::Ok) {
        throw std::runtime_error("Failed to initialize UART HAL");
    }
    if (can_->initialize(config_.can) != Status::Ok) {
        throw std::runtime_error("Failed to initialize CAN HAL");
    }
    if (storage_->mount(config_.storage) != Status::Ok) {
        throw std::runtime_error("Failed to mount storage HAL");
    }
    if (watchdog_->configure(config_.watchdog) != Status::Ok) {
        throw std::runtime_error("Failed to configure watchdog HAL");
    }

    initialized_ = true;
}

IHalUart& HalManager::uart() {
    if (!uart_) {
        throw std::runtime_error("UART HAL not available");
    }
    return *uart_;
}

IHalCan& HalManager::can() {
    if (!can_) {
        throw std::runtime_error("CAN HAL not available");
    }
    return *can_;
}

IHalStorage& HalManager::storage() {
    if (!storage_) {
        throw std::runtime_error("Storage HAL not available");
    }
    return *storage_;
}

IHalWatchdog& HalManager::watchdog() {
    if (!watchdog_) {
        throw std::runtime_error("Watchdog HAL not available");
    }
    return *watchdog_;
}

bool HalManager::isInitialized() const {
    return initialized_;
}

} // namespace hal
