#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hal {

enum class Status {
    Ok,
    Error,
    Timeout,
    Busy,
    Unsupported,
    InvalidArgument
};

enum class GpioMode {
    Input,
    Output,
    InputPullUp,
    InputPullDown,
    OpenDrain
};

enum class GpioLevel : uint8_t {
    Low = 0,
    High = 1
};

enum class GpioPull {
    None,
    Up,
    Down
};

struct CanFrame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    std::array<uint8_t, 8> data{ {0} };
    bool extended = false;
};

struct CanStats {
    uint32_t tx_success = 0;
    uint32_t tx_errors = 0;
    uint32_t rx_success = 0;
    uint32_t rx_errors = 0;
    uint32_t rx_dropped = 0;
    uint32_t bus_off_events = 0;
};

struct TimerContext {
    void* user = nullptr;
};

using TimerCallback = std::function<void(TimerContext)>;

enum class StorageType {
    SPIFFS,
    NVS
};

enum class StorageOpenMode {
    Read,
    Write,
    Append
};

struct WatchdogStats {
    uint32_t feed_count = 0;
    uint32_t min_interval_ms = 0;
    uint32_t max_interval_ms = 0;
    float average_interval_ms = 0.0f;
};

} // namespace hal
