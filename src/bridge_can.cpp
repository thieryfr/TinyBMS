/**
 * @file bridge_can.cpp
 * @brief CAN TX (PGNs) + RX polling (KeepAlive)
 */
#include <Arduino.h>
#include <string.h>
#include <math.h>
#include "bridge_can.h"
#include "bridge_keepalive.h"
#include "logger.h"
#include "event_bus.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "can_driver.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t feedMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[CAN] ") + (msg)); } while(0)

static inline void put_u16_le(uint8_t* b, uint16_t v){ b[0]=v & 0xFF; b[1]=(v>>8)&0xFF; }
static inline void put_s16_le(uint8_t* b, int16_t v){ b[0]=v & 0xFF; b[1]=(v>>8)&0xFF; }
static inline uint16_t clamp_u16(int v){ return (uint16_t) (v<0?0:(v>65535?65535:v)); }
static inline int16_t  clamp_s16(int v){ return (int16_t)  (v<-32768?-32768:(v>32767?32767:v)); }
static inline int      round_i(float x){ return (int)lrintf(x); }

bool TinyBMS_Victron_Bridge::sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc) {
    CanFrame f; f.id = pgn_id; f.dlc = dlc; f.extended = false; memcpy(f.data, data, dlc);
    bool ok = CanDriver::send(f);
    CanDriverStats driverStats = CanDriver::getStats();
    stats.can_tx_count = driverStats.tx_success;
    stats.can_tx_errors = driverStats.tx_errors;
    stats.can_rx_errors = driverStats.rx_errors;
    stats.can_bus_off_count = driverStats.bus_off_events;
    stats.can_queue_overflows = driverStats.rx_dropped;

    if (ok) {
        if (config.logging.log_can_traffic) BRIDGE_LOG(LOG_DEBUG, String("TX PGN 0x") + String(pgn_id, HEX));
    } else {
        eventBus.publishAlarm(ALARM_CAN_TX_ERROR, "CAN TX failed", ALARM_SEVERITY_WARNING, pgn_id, SOURCE_ID_CAN);
        BRIDGE_LOG(LOG_WARN, String("TX failed PGN 0x") + String(pgn_id, HEX));
    }
    return ok;
}

void TinyBMS_Victron_Bridge::buildPGN_0x356(uint8_t* d){
    const auto& ld = live_data_;
    uint16_t u_001V = clamp_u16(round_i(ld.voltage * 100.0f));
    int16_t  i_01A  = clamp_s16(round_i(ld.current * 10.0f));
    int16_t  t_01C  = clamp_s16((int)ld.temperature); // already in 0.1 C
    put_u16_le(&d[0], u_001V);
    put_s16_le(&d[2], i_01A);
    put_s16_le(&d[4], t_01C);
    d[6]=d[7]=0;
}

void TinyBMS_Victron_Bridge::buildPGN_0x355(uint8_t* d){
    const auto& ld = live_data_;
    uint16_t soc_01 = clamp_u16(round_i(ld.soc_percent * 10.0f));
    uint16_t soh_01 = clamp_u16(round_i(ld.soh_percent * 10.0f));
    put_u16_le(&d[0], soc_01);
    put_u16_le(&d[2], soh_01);
    d[4]=d[5]=d[6]=d[7]=0;
}

void TinyBMS_Victron_Bridge::buildPGN_0x351(uint8_t* d){
    const auto& ld = live_data_;
    float cvl_target_v = stats.cvl_current_v > 0.0f ? stats.cvl_current_v : ld.voltage;
    float ccl_limit_a = stats.ccl_limit_a > 0.0f ? stats.ccl_limit_a : (ld.max_charge_current / 10.0f);
    float dcl_limit_a = stats.dcl_limit_a > 0.0f ? stats.dcl_limit_a : (ld.max_discharge_current / 10.0f);

    uint16_t cvl_001V = clamp_u16(round_i(cvl_target_v * 100.0f));
    uint16_t ccl_01A  = clamp_u16(round_i(ccl_limit_a * 10.0f));
    uint16_t dcl_01A  = clamp_u16(round_i(dcl_limit_a * 10.0f));
    put_u16_le(&d[0], cvl_001V);
    put_u16_le(&d[2], ccl_01A);
    put_u16_le(&d[4], dcl_01A);
    d[6]=d[7]=0;
}

void TinyBMS_Victron_Bridge::buildPGN_0x35A(uint8_t* d){
    memset(d, 0, 8);
    const auto& ld = live_data_;
    const auto& th = config.victron.thresholds;

    const float V = ld.voltage;
    const float T = ld.temperature / 10.0f;
    const uint16_t imbalance = ld.cell_imbalance_mv;

    uint8_t b0 = 0;
    auto set = [](bool condAlarm, bool condWarn)->uint8_t{
        return condAlarm ? 2 : (condWarn ? 1 : 0);
    };

    b0 = encode2bit(b0, 0, set(V < th.undervoltage_v && V > 0.1f, false));
    b0 = encode2bit(b0, 1, set(V > th.overvoltage_v, false));
    b0 = encode2bit(b0, 2, set(T > th.overtemp_c, false));
    b0 = encode2bit(b0, 3, set(T < th.low_temp_charge_c && ld.current > 3.0f, false));
    d[0] = b0;

    uint8_t b1 = 0;
    b1 = encode2bit(b1, 0, set(imbalance > th.imbalance_alarm_mv, imbalance > th.imbalance_warn_mv));
    bool commErr = (stats.uart_errors > 0 || stats.can_tx_errors > 0 || !stats.victron_keepalive_ok);
    b1 = encode2bit(b1, 1, set(false, commErr));

    bool lowSOC  = (ld.soc_percent <= th.soc_low_percent);
    bool highSOC = (ld.soc_percent >= th.soc_high_percent);
    uint16_t minLimit = static_cast<uint16_t>(th.derate_current_a * 10.0f);
    bool derate = (ld.max_charge_current <= minLimit || ld.max_discharge_current <= minLimit);

    b1 = encode2bit(b1, 2, set(false, lowSOC));
    b1 = encode2bit(b1, 3, set(false, derate || highSOC));
    d[1] = b1;

    d[7] = 0;
    d[7] = encode2bit(d[7], 0, (commErr || (V<th.undervoltage_v) || (V>th.overvoltage_v) || (T>th.overtemp_c)) ? 2 : 1);
}

void TinyBMS_Victron_Bridge::buildPGN_0x35E(uint8_t* d){
    memset(d, 0, 8);
    const String& m = config.victron.manufacturer_name;
    for (int i=0;i<8 && i<(int)m.length();++i) d[i] = (uint8_t)m[i];
}
void TinyBMS_Victron_Bridge::buildPGN_0x35F(uint8_t* d){
    memset(d, 0, 8);
    const String& n = config.victron.battery_name;
    for (int i=0;i<8 && i<(int)n.length();++i) d[i] = (uint8_t)n[i];
}

void TinyBMS_Victron_Bridge::canTask(void *pvParameters){
    auto *bridge = static_cast<TinyBMS_Victron_Bridge*>(pvParameters);
    BRIDGE_LOG(LOG_INFO, "canTask started");

    while (true) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        bridge->keepAliveProcessRX(now);

        if (now - bridge->last_pgn_update_ms_ >= bridge->pgn_update_interval_ms_) {
            TinyBMS_LiveData d;
            if (eventBus.getLatestLiveData(d)) bridge->live_data_ = d;

            uint8_t p[8];

            memset(p,0,8); bridge->buildPGN_0x356(p); bridge->sendVictronPGN(VICTRON_PGN_VOLTAGE_CURRENT, p, 8);
            memset(p,0,8); bridge->buildPGN_0x355(p); bridge->sendVictronPGN(VICTRON_PGN_SOC_SOH, p, 8);
            memset(p,0,8); bridge->buildPGN_0x351(p); bridge->sendVictronPGN(VICTRON_PGN_CVL_CCL_DCL, p, 8);
            memset(p,0,8); bridge->buildPGN_0x35A(p); bridge->sendVictronPGN(VICTRON_PGN_ALARMS, p, 8);
            memset(p,0,8); bridge->buildPGN_0x35E(p); bridge->sendVictronPGN(VICTRON_PGN_MANUFACTURER, p, 8);
            memset(p,0,8); bridge->buildPGN_0x35F(p); bridge->sendVictronPGN(VICTRON_PGN_BATTERY_INFO, p, 8);

            bridge->keepAliveSend();

            bridge->last_pgn_update_ms_ = now;

            if (xSemaphoreTake(feedMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                Watchdog.feed();
                xSemaphoreGive(feedMutex);
            }
        }

        CanDriverStats driverStats = CanDriver::getStats();
        bridge->stats.can_tx_count = driverStats.tx_success;
        bridge->stats.can_tx_errors = driverStats.tx_errors;
        bridge->stats.can_rx_errors = driverStats.rx_errors;
        bridge->stats.can_bus_off_count = driverStats.bus_off_events;
        bridge->stats.can_queue_overflows = driverStats.rx_dropped;

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
