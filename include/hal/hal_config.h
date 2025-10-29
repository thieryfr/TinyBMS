#pragma once

#include <cstdint>
#include <vector>
#include "hal/hal_types.h"

namespace hal {

struct UartConfig {
    int rx_pin = -1;
    int tx_pin = -1;
    uint32_t baudrate = 115200;
    uint32_t timeout_ms = 1000;
    bool use_dma = false;
};

struct CanFilterConfig {
    uint32_t id = 0;
    uint32_t mask = 0;
    bool extended = false;
};

struct CanConfig {
    int tx_pin = -1;
    int rx_pin = -1;
    uint32_t bitrate = 250000;
    bool enable_termination = true;
    std::vector<CanFilterConfig> filters;
};

struct StorageConfig {
    StorageType type = StorageType::SPIFFS;
    bool format_on_fail = false;
};

struct GpioConfig {
    int pin = -1;
    GpioMode mode = GpioMode::Input;
    GpioPull pull = GpioPull::None;
    GpioLevel initial_level = GpioLevel::Low;
};

struct TimerConfig {
    uint32_t period_ms = 1000;
    bool auto_reload = true;
};

struct WatchdogConfig {
    uint32_t timeout_ms = 5000;
};

struct HalConfig {
    UartConfig uart;
    CanConfig can;
    StorageConfig storage;
    WatchdogConfig watchdog;
};

} // namespace hal
