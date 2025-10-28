/**
 * @file bridge_cvl.cpp
 * @brief CVL task implementation
 */

#include <Arduino.h>
#include <algorithm>

#include "bridge_cvl.h"
#include "logger.h"
#include "event_bus.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "config_manager.h"
#include "cvl_logic.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[CVL] ") + (msg)); } while(0)

namespace {

CVLConfigSnapshot loadConfigSnapshot(const TinyBMS_LiveData& data) {
    CVLConfigSnapshot snapshot;
    snapshot.bulk_target_voltage_v = data.voltage;

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snapshot.enabled = config.cvl.enabled;
        snapshot.bulk_soc_threshold = config.cvl.bulk_soc_threshold;
        snapshot.transition_soc_threshold = config.cvl.transition_soc_threshold;
        snapshot.float_soc_threshold = config.cvl.float_soc_threshold;
        snapshot.float_exit_soc = config.cvl.float_exit_soc;
        snapshot.float_approach_offset_mv = config.cvl.float_approach_offset_mv;
        snapshot.float_offset_mv = config.cvl.float_offset_mv;
        snapshot.minimum_ccl_in_float_a = config.cvl.minimum_ccl_in_float_a;
        snapshot.imbalance_hold_threshold_mv = config.cvl.imbalance_hold_threshold_mv;
        snapshot.imbalance_release_threshold_mv = config.cvl.imbalance_release_threshold_mv;
        snapshot.bulk_target_voltage_v = config.victron.thresholds.overvoltage_v;
        if (snapshot.bulk_target_voltage_v <= 0.0f) {
            snapshot.bulk_target_voltage_v = data.voltage;
        }
        xSemaphoreGive(configMutex);
    }

    return snapshot;
}

bool shouldLogChanges() {
    bool enabled = false;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        enabled = config.logging.log_cvl_changes;
        xSemaphoreGive(configMutex);
    }
    return enabled;
}

void logChangeIfNeeded(const CVLComputationResult& result,
                       CVLState previous_state,
                       const TinyBMS_LiveData& data) {
    if (!shouldLogChanges()) return;

    BRIDGE_LOG(LOG_INFO,
               String("State ") + previous_state + " → " + result.state +
               ", CVL=" + String(result.cvl_voltage_v, 2) + "V, " +
               "CCL=" + String(result.ccl_limit_a, 2) + "A, " +
               "DCL=" + String(result.dcl_limit_a, 2) + "A, " +
               "SOC=" + String(data.soc_percent, 1) + "%");
}

} // namespace

void TinyBMS_Victron_Bridge::cvlTask(void *pvParameters){
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "cvlTask started");

    CVLState last_state = bridge->stats.cvl_state;
    uint32_t state_entry_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_cvl_update_ms_ >= bridge->cvl_update_interval_ms_) {
            TinyBMS_LiveData data;
            if (eventBus.getLatestLiveData(data)) {
                CVLInputs inputs;
                inputs.soc_percent = data.soc_percent;
                inputs.cell_imbalance_mv = data.cell_imbalance_mv;
                inputs.pack_voltage_v = data.voltage;
                inputs.base_ccl_limit_a = data.max_charge_current / 10.0f;
                inputs.base_dcl_limit_a = data.max_discharge_current / 10.0f;

                CVLConfigSnapshot snapshot = loadConfigSnapshot(data);
                if (snapshot.bulk_target_voltage_v <= 0.0f) {
                    snapshot.bulk_target_voltage_v = std::max(inputs.pack_voltage_v, 0.0f);
                }

                CVLComputationResult result = computeCvlLimits(inputs, snapshot, last_state);

                bridge->stats.cvl_state = result.state;
                bridge->stats.cvl_current_v = result.cvl_voltage_v;
                bridge->stats.ccl_limit_a = result.ccl_limit_a;
                bridge->stats.dcl_limit_a = result.dcl_limit_a;

                if (result.state != last_state) {
                    uint32_t duration = now - state_entry_ms;
                    eventBus.publishCVLStateChange(static_cast<uint8_t>(last_state),
                                                   static_cast<uint8_t>(result.state),
                                                   result.cvl_voltage_v,
                                                   result.ccl_limit_a,
                                                   result.dcl_limit_a,
                                                   duration,
                                                   SOURCE_ID_CVL);
                    logChangeIfNeeded(result, last_state, data);
                    last_state = result.state;
                    state_entry_ms = now;
                }

                logger.log(LOG_DEBUG,
                           String("[CVL] target=") + String(result.cvl_voltage_v, 2) +
                           "V CCL=" + String(result.ccl_limit_a, 1) +
                           "A DCL=" + String(result.dcl_limit_a, 1) + "A");
            }

            bridge->last_cvl_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(bridge->cvl_update_interval_ms_));
    }
}

