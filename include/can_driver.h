#pragma once

#include <Arduino.h>

struct CanFrame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {0};
    bool extended = false;
};

struct CanDriverStats {
    uint32_t tx_success = 0;
    uint32_t tx_errors = 0;
    uint32_t rx_success = 0;
    uint32_t rx_errors = 0;
    uint32_t rx_dropped = 0;
    uint32_t bus_off_events = 0;
};

class CanDriver {
public:
    static bool begin(int tx_pin, int rx_pin, uint32_t bitrate);
    static bool send(const CanFrame& frame);
    static bool receive(CanFrame& frame);
    static CanDriverStats getStats();
    static void resetStats();
};
