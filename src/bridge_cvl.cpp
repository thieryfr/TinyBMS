
/**
 * @file bridge_cvl.cpp
 * @brief CVL task (simple demo implementation)
 */
#include <Arduino.h>
#include "bridge_cvl.h"
#include "logger.h"
#include "event_bus.h"
#include "watchdog_manager.h"
#include "rtos_config.h"

extern Logger logger;
extern EventBus& eventBus;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[CVL] ") + msg); } while(0)

void TinyBMS_Victron_Bridge::cvlTask(void *pvParameters){
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "cvlTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (now - bridge->last_cvl_update_ms_ >= CVL_UPDATE_INTERVAL_MS) {
            TinyBMS_LiveData d;
            if (eventBus.getLatestLiveData(d)) {
                bridge->stats.cvl_current_v = d.voltage;
                bridge->stats.cvl_state = CVL_BULK_ABSORPTION;
                logger.log(LOG_DEBUG, String("[CVL] setpoint=") + String(bridge->stats.cvl_current_v, 2) + "V");
            }
            bridge->last_cvl_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(CVL_UPDATE_INTERVAL_MS));
    }
}
