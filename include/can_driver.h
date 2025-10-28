#pragma once

#include <Arduino.h>

struct CanFrame {
    uint32_t id = 0;
    uint8_t dlc = 0;
    uint8_t data[8] = {0};
    bool extended = false;
};

class CanDriver {
public:
    static bool begin(int tx_pin, int rx_pin, uint32_t bitrate);
    static bool send(const CanFrame& frame);
    static bool receive(CanFrame& frame);
};
