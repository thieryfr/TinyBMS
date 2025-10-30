#pragma once

#include "system_config.hpp"
#include "esp_err.h"

namespace tinybms {

esp_err_t wifi_manager_start(const SystemConfig &config);
esp_err_t wifi_manager_update(const SystemConfig &config);

} // namespace tinybms
