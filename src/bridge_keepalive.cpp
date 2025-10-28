/**
 * @file bridge_keepalive.cpp
 * @brief KeepAlive (0x305) bidirectional: RX detect + TX heartbeat
 */
#include <Arduino.h>
#include "bridge_keepalive.h"
#include "logger.h"
#include "event_bus.h"
#include "config_manager.h"
#include "can_driver.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[KA] ") + (msg)); } while(0)

void TinyBMS_Victron_Bridge::keepAliveSend(){
    uint32_t now = millis();
    if (now - last_keepalive_tx_ms_ < keepalive_interval_ms_) return;
    uint8_t d[8] = {0};
    sendVictronPGN(VICTRON_PGN_KEEPALIVE, d, 1);
    last_keepalive_tx_ms_ = now;
}

void TinyBMS_Victron_Bridge::keepAliveProcessRX(uint32_t now_ms){
    CanFrame f;
    while (CanDriver::receive(f)) {
        stats.can_rx_count++;
        if (!f.extended && f.id == VICTRON_PGN_KEEPALIVE) {
            last_keepalive_rx_ms_ = now_ms;
            if (!victron_keepalive_ok_) {
                victron_keepalive_ok_ = true;
                stats.victron_keepalive_ok = true;
                // Inform observers (WebSocket, REST) that the keep-alive is healthy again
                eventBus.publishStatus("VE.Can keepalive OK", SOURCE_ID_CAN, STATUS_LEVEL_INFO);
                BRIDGE_LOG(LOG_INFO, "VE.Can keepalive detected");
            }
        }
    }

    if (victron_keepalive_ok_ && (now_ms - last_keepalive_rx_ms_ > keepalive_timeout_ms_)) {
        victron_keepalive_ok_ = false;
        stats.victron_keepalive_ok = false;
        eventBus.publishAlarm(ALARM_CAN_KEEPALIVE_LOST, "VE.Can keepalive lost", ALARM_SEVERITY_WARNING, 0, SOURCE_ID_CAN);
        BRIDGE_LOG(LOG_WARN, "VE.Can keepalive TIMEOUT");
    }
}
