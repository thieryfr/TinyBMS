#pragma once

#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/twai.h"
#include <cstdint>
#include <string>

namespace tinybms {

struct BridgePins {
    gpio_num_t uart_tx;
    gpio_num_t uart_rx;
    gpio_num_t can_tx;
    gpio_num_t can_rx;
    gpio_num_t status_led;
};

struct BridgeTimings {
    uint32_t uart_baudrate;
    uint32_t sample_queue_length;
    uint32_t keepalive_period_ms;
    uint32_t diagnostic_period_ms;
};

struct MqttTopics {
    std::string root;
    std::string telemetry;
    std::string status;
};

struct MqttConfig {
    bool enabled;
    std::string broker_host;
    uint16_t port;
    MqttTopics topics;
};

struct BridgeConfig {
    uart_port_t uart_port;
    BridgePins pins;
    BridgeTimings timings;
    twai_general_config_t can_general;
    twai_timing_config_t can_timing;
    twai_filter_config_t can_filter;
    MqttConfig mqtt;
};

BridgeConfig load_bridge_config();

} // namespace tinybms
