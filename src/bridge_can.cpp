/**
 * @file bridge_can.cpp
 * @brief CAN TX (PGNs) + RX polling (KeepAlive)
 */
#include <Arduino.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "bridge_can.h"
#include "bridge_keepalive.h"
#include "logger.h"
#include "event_bus.h"
#include "config_manager.h"
#include "victron_can_mapping.h"
#include "watchdog_manager.h"
#include "rtos_config.h"
#include "can_driver.h"

extern Logger logger;
extern EventBus& eventBus;
extern ConfigManager config;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[CAN] ") + (msg)); } while(0)

static inline void put_u16_le(uint8_t* b, uint16_t v){ b[0]=v & 0xFF; b[1]=(v>>8)&0xFF; }
static inline void put_s16_le(uint8_t* b, int16_t v){ b[0]=v & 0xFF; b[1]=(v>>8)&0xFF; }
static inline uint16_t clamp_u16(int v){ return (uint16_t) (v<0?0:(v>65535?65535:v)); }
static inline int16_t  clamp_s16(int v){ return (int16_t)  (v<-32768?-32768:(v>32767?32767:v)); }
static inline int      round_i(float x){ return (int)lrintf(x); }

namespace {

struct VictronMappingContext {
    const TinyBMS_LiveData& live;
    const BridgeStats& stats;
    ConfigManager::VictronConfig::Thresholds thresholds{};
    bool thresholds_loaded = false;
    bool comm_error_cached = false;
    bool comm_error_value = false;
    bool derate_cached = false;
    bool derate_value = false;
};

bool ensureThresholds(VictronMappingContext& ctx) {
    if (ctx.thresholds_loaded) {
        return true;
    }
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        ctx.thresholds = config.victron.thresholds;
        ctx.thresholds_loaded = true;
        xSemaphoreGive(configMutex);
        return true;
    }
    return false;
}

bool getLiveDataValue(TinyLiveDataField field, const TinyBMS_LiveData& live, float& value) {
    switch (field) {
        case TinyLiveDataField::Voltage:
            value = live.voltage;
            return true;
        case TinyLiveDataField::Current:
            value = live.current;
            return true;
        case TinyLiveDataField::SocPercent:
            value = live.soc_percent;
            return true;
        case TinyLiveDataField::SohPercent:
            value = live.soh_percent;
            return true;
        case TinyLiveDataField::Temperature:
            value = static_cast<float>(live.temperature);
            return true;
        case TinyLiveDataField::MinCellMv:
            value = static_cast<float>(live.min_cell_mv);
            return true;
        case TinyLiveDataField::MaxCellMv:
            value = static_cast<float>(live.max_cell_mv);
            return true;
        case TinyLiveDataField::BalancingBits:
            value = static_cast<float>(live.balancing_bits);
            return true;
        case TinyLiveDataField::MaxChargeCurrent:
            value = static_cast<float>(live.max_charge_current) / 10.0f;
            return true;
        case TinyLiveDataField::MaxDischargeCurrent:
            value = static_cast<float>(live.max_discharge_current) / 10.0f;
            return true;
        case TinyLiveDataField::OnlineStatus:
            value = static_cast<float>(live.online_status);
            return true;
        case TinyLiveDataField::CellImbalanceMv:
            value = static_cast<float>(live.cell_imbalance_mv);
            return true;
        case TinyLiveDataField::NeedBalancing:
        case TinyLiveDataField::None:
        default:
            break;
    }
    return false;
}

bool computeCommError(VictronMappingContext& ctx) {
    if (!ctx.comm_error_cached) {
        ctx.comm_error_value = (ctx.stats.uart_errors > 0 || ctx.stats.can_tx_errors > 0 || !ctx.stats.victron_keepalive_ok);
        ctx.comm_error_cached = true;
    }
    return ctx.comm_error_value;
}

bool computeDerate(VictronMappingContext& ctx) {
    if (!ctx.derate_cached) {
        ensureThresholds(ctx);
        const uint16_t minLimit = static_cast<uint16_t>(ctx.thresholds.derate_current_a * 10.0f);
        ctx.derate_value = (ctx.live.max_charge_current <= minLimit || ctx.live.max_discharge_current <= minLimit);
        ctx.derate_cached = true;
    }
    return ctx.derate_value;
}

bool computeFunctionValue(const VictronCanFieldDefinition& field,
                          const TinyBMS_Victron_Bridge& bridge,
                          VictronMappingContext& ctx,
                          float& value) {
    String id = field.source.identifier;
    id.toLowerCase();

    const auto& live = ctx.live;
    const auto& stats = ctx.stats;

    if (id == "cvl_dynamic") {
        float cvl = stats.cvl_current_v > 0.0f ? stats.cvl_current_v : live.voltage;
        value = cvl;
        return true;
    }
    if (id == "ccl_limit") {
        float ccl = stats.ccl_limit_a > 0.0f ? stats.ccl_limit_a : (static_cast<float>(live.max_charge_current) / 10.0f);
        value = ccl;
        return true;
    }
    if (id == "dcl_limit") {
        float dcl = stats.dcl_limit_a > 0.0f ? stats.dcl_limit_a : (static_cast<float>(live.max_discharge_current) / 10.0f);
        value = dcl;
        return true;
    }

    ensureThresholds(ctx);
    const auto& th = ctx.thresholds;
    const float voltage = live.voltage;
    const float temperature_c = static_cast<float>(live.temperature) / 10.0f;
    const uint16_t imbalance = live.cell_imbalance_mv;
    const bool commErr = computeCommError(ctx);
    const bool lowSOC = (live.soc_percent <= th.soc_low_percent);
    const bool highSOC = (live.soc_percent >= th.soc_high_percent);
    const bool derate = computeDerate(ctx);

    if (id == "alarm_undervoltage") {
        value = (voltage < th.undervoltage_v && voltage > 0.1f) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_overvoltage") {
        value = (voltage > th.overvoltage_v) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_overtemperature") {
        value = (temperature_c > th.overtemp_c) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_low_temp_charge") {
        value = (temperature_c < th.low_temp_charge_c && live.current > 3.0f) ? 2.0f : 0.0f;
        return true;
    }
    if (id == "alarm_cell_imbalance") {
        value = (imbalance > th.imbalance_alarm_mv) ? 2.0f : ((imbalance > th.imbalance_warn_mv) ? 1.0f : 0.0f);
        return true;
    }
    if (id == "alarm_comms") {
        value = commErr ? 1.0f : 0.0f;
        return true;
    }
    if (id == "warn_low_soc") {
        value = lowSOC ? 1.0f : 0.0f;
        return true;
    }
    if (id == "warn_derate_high_soc") {
        value = (derate || highSOC) ? 1.0f : 0.0f;
        return true;
    }
    if (id == "summary_status") {
        const bool alarm = commErr || (voltage < th.undervoltage_v) || (voltage > th.overvoltage_v) || (temperature_c > th.overtemp_c);
        value = alarm ? 2.0f : 1.0f;
        return true;
    }

    return false;
}

float applyConversionValue(const VictronFieldConversion& conv, float raw) {
    float value = (raw * conv.gain) + conv.offset;
    if (conv.round) {
        value = static_cast<float>(lrintf(value));
    }
    if (conv.has_min) {
        value = std::max(conv.min_value, value);
    }
    if (conv.has_max) {
        value = std::min(conv.max_value, value);
    }
    return value;
}

bool writeFieldValue(uint8_t* data, const VictronCanFieldDefinition& field, float value) {
    if (field.encoding == VictronFieldEncoding::Bits) {
        if (field.byte_offset >= 8 || field.bit_length == 0 || field.bit_length > 8) {
            return false;
        }
        uint8_t raw = field.conversion.round ? static_cast<uint8_t>(lrintf(value)) : static_cast<uint8_t>(value);
        const uint8_t mask_base = static_cast<uint8_t>((field.bit_length >= 8 ? 0xFFu : ((1u << field.bit_length) - 1u)));
        const uint8_t mask_shifted = static_cast<uint8_t>(mask_base << field.bit_offset);
        const uint8_t value_shifted = static_cast<uint8_t>((raw & mask_base) << field.bit_offset);
        data[field.byte_offset] &= ~mask_shifted;
        data[field.byte_offset] |= value_shifted;
        return true;
    }

    if (field.byte_offset >= 8 || field.length == 0 || field.byte_offset + field.length > 8) {
        return false;
    }

    int32_t raw_int = field.conversion.round ? static_cast<int32_t>(lrintf(value)) : static_cast<int32_t>(value);

    if (field.encoding == VictronFieldEncoding::Unsigned) {
        uint32_t raw = static_cast<uint32_t>(raw_int);
        for (uint8_t i = 0; i < field.length; ++i) {
            data[field.byte_offset + i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFFu);
        }
        return true;
    }

    int32_t raw = raw_int;
    for (uint8_t i = 0; i < field.length; ++i) {
        data[field.byte_offset + i] = static_cast<uint8_t>((raw >> (8 * i)) & 0xFFu);
    }
    return true;
}

bool applyVictronMapping(const TinyBMS_Victron_Bridge& bridge, uint16_t pgn, uint8_t* data) {
    const VictronPgnDefinition* def = findVictronPgnDefinition(pgn);
    if (!def) {
        return false;
    }

    VictronMappingContext ctx{bridge.live_data_, bridge.stats};
    bool wrote_any = false;

    for (const auto& field : def->fields) {
        float source_value = 0.0f;
        bool has_value = false;

        switch (field.source.type) {
            case VictronValueSourceType::LiveData:
                has_value = getLiveDataValue(field.source.live_field, bridge.live_data_, source_value);
                break;
            case VictronValueSourceType::Function:
                has_value = computeFunctionValue(field, bridge, ctx, source_value);
                break;
            case VictronValueSourceType::Constant:
                source_value = field.source.constant;
                has_value = true;
                break;
            default:
                break;
        }

        if (!has_value) {
            continue;
        }

        float converted = applyConversionValue(field.conversion, source_value);
        if (writeFieldValue(data, field, converted)) {
            wrote_any = true;
        }
    }

    return wrote_any;
}

} // namespace

bool TinyBMS_Victron_Bridge::sendVictronPGN(uint16_t pgn_id, const uint8_t* data, uint8_t dlc) {
    CanFrame f; f.id = pgn_id; f.dlc = dlc; f.extended = false; memcpy(f.data, data, dlc);
    bool ok = CanDriver::send(f);
    CanDriverStats driverStats = CanDriver::getStats();
    stats.can_tx_count = driverStats.tx_success;
    stats.can_tx_errors = driverStats.tx_errors;
    stats.can_rx_errors = driverStats.rx_errors;
    stats.can_bus_off_count = driverStats.bus_off_events;
    stats.can_queue_overflows = driverStats.rx_dropped;

    bool log_can_traffic = false;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        log_can_traffic = config.logging.log_can_traffic;
        xSemaphoreGive(configMutex);
    }

    if (ok) {
        if (log_can_traffic) BRIDGE_LOG(LOG_DEBUG, String("TX PGN 0x") + String(pgn_id, HEX));
    } else {
        eventBus.publishAlarm(ALARM_CAN_TX_ERROR, "CAN TX failed", ALARM_SEVERITY_WARNING, pgn_id, SOURCE_ID_CAN);
        BRIDGE_LOG(LOG_WARN, String("TX failed PGN 0x") + String(pgn_id, HEX));
    }
    return ok;
}

void TinyBMS_Victron_Bridge::buildPGN_0x356(uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, VICTRON_PGN_VOLTAGE_CURRENT, d)) {
        return;
    }

    const auto& ld = live_data_;
    uint16_t u_001V = clamp_u16(round_i(ld.voltage * 100.0f));
    int16_t  i_01A  = clamp_s16(round_i(ld.current * 10.0f));
    int16_t  t_01C  = clamp_s16((int)ld.temperature); // already in 0.1 C
    put_u16_le(&d[0], u_001V);
    put_s16_le(&d[2], i_01A);
    put_s16_le(&d[4], t_01C);
}

void TinyBMS_Victron_Bridge::buildPGN_0x355(uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, VICTRON_PGN_SOC_SOH, d)) {
        return;
    }

    const auto& ld = live_data_;
    uint16_t soc_01 = clamp_u16(round_i(ld.soc_percent * 10.0f));
    uint16_t soh_01 = clamp_u16(round_i(ld.soh_percent * 10.0f));
    put_u16_le(&d[0], soc_01);
    put_u16_le(&d[2], soh_01);
}

void TinyBMS_Victron_Bridge::buildPGN_0x351(uint8_t* d){
    memset(d, 0, 8);
    if (applyVictronMapping(*this, VICTRON_PGN_CVL_CCL_DCL, d)) {
        return;
    }

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
    if (applyVictronMapping(*this, VICTRON_PGN_ALARMS, d)) {
        return;
    }

    const auto& ld = live_data_;
    ConfigManager::VictronConfig::Thresholds th{};
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        th = config.victron.thresholds;
        xSemaphoreGive(configMutex);
    }

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
    String m = "TinyBMS";
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        m = config.victron.manufacturer_name;
        xSemaphoreGive(configMutex);
    }
    for (int i=0;i<8 && i<(int)m.length();++i) d[i] = (uint8_t)m[i];
}
void TinyBMS_Victron_Bridge::buildPGN_0x35F(uint8_t* d){
    memset(d, 0, 8);
    String n = "Lithium Battery";
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(25)) == pdTRUE) {
        n = config.victron.battery_name;
        xSemaphoreGive(configMutex);
    }
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
