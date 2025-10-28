#include "can_driver.h"

bool CanDriver::begin(int tx_pin, int rx_pin, uint32_t bitrate) {
    (void)tx_pin;
    (void)rx_pin;
    (void)bitrate;
    return true;
}

bool CanDriver::send(const CanFrame& frame) {
    (void)frame;
    return true;
}

bool CanDriver::receive(CanFrame& frame) {
    (void)frame;
    return false;
}
