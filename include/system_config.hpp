#pragma once

#include "esp_err.h"
#include <cstdint>
#include <string>

namespace tinybms {

struct WifiStaConfig {
    bool enabled = false;
    std::string ssid;
    std::string password;
};

struct WifiApConfig {
    std::string ssid;
    std::string password;
    uint8_t channel = 1;
    uint8_t max_connections = 4;
};

struct WebServerConfig {
    bool enable_websocket = true;
    bool enable_cors = true;
    std::string cors_origin = "*";
};

struct SystemConfig {
    std::string device_name = "tinybms";
    WifiStaConfig sta;
    WifiApConfig ap;
    WebServerConfig web;
};

esp_err_t load_system_config(SystemConfig &config);
esp_err_t save_system_config(const SystemConfig &config);

} // namespace tinybms
