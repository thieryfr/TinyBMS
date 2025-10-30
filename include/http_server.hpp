#pragma once

#include "bridge.hpp"
#include "system_config.hpp"
#include "esp_err.h"

namespace tinybms {

struct HttpServerHandle {
    void *handle = nullptr;
    void *ctx = nullptr;
};

esp_err_t start_http_server(HttpServerHandle &server, SystemConfig &config, TinyBmsBridge &bridge);
void stop_http_server(HttpServerHandle &server);

} // namespace tinybms
