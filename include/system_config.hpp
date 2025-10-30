#pragma once

#include "esp_err.h"
#include "logger.hpp"
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
    uint32_t websocket_update_interval_ms = 1000;
    uint8_t max_ws_clients = 4;
    bool enable_auth = false;
    std::string username = "admin";
    std::string password = "tinybms";
};

struct LoggingConfig {
    log::Level level = log::Level::Info;
    bool web_enabled = true;
    bool serial_enabled = true;
};

struct SystemConfig {
    std::string device_name = "tinybms";
    WifiStaConfig sta;
    WifiApConfig ap;
    WebServerConfig web;
    LoggingConfig logging;
};

esp_err_t load_system_config(SystemConfig &config);
esp_err_t save_system_config(const SystemConfig &config);

} // namespace tinybms
