/**
 * @file shared_data.h
 * @brief Shared data structures for FreeRTOS tasks + Logging utilities
 */

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>

#include "tiny_read_mapping.h"

constexpr size_t TINY_LIVEDATA_MAX_REGISTERS = 10;

struct TinyRegisterSnapshot {
    int32_t raw_value;
    uint16_t address;
    uint8_t raw_word_count;
    uint8_t type;
};

// ====================================================================================
// STRUCTURE PRINCIPALE
// ====================================================================================

/**
 * @struct TinyBMS_LiveData
 * @brief Structure de données partagées entre tâches UART, CAN, WebSocket
 */
struct TinyBMS_LiveData {
    float voltage;               // V
    float current;               // A (negative = discharge)
    uint16_t min_cell_mv;        // mV
    uint16_t max_cell_mv;        // mV
    uint16_t soc_raw;            // Raw SOC (scale 0.002%)
    uint16_t soh_raw;            // Raw SOH (scale 0.002%)
    int16_t temperature;         // 0.1°C
    uint16_t online_status;      // 0x91-0x97 = OK, 0x9B = Fault
    uint16_t balancing_bits;     // Bitfield: active cell balancing
    uint16_t max_discharge_current; // 0.1A
    uint16_t max_charge_current;    // 0.1A
    float soc_percent;           // 0–100%
    float soh_percent;           // 0–100%
    uint16_t cell_imbalance_mv;  // Max - Min cell diff (mV)
    uint16_t register_count; // Dynamic register snapshots count
    TinyRegisterSnapshot register_snapshots[TINY_LIVEDATA_MAX_REGISTERS];

    /**
     * @brief Retourne une représentation textuelle formatée (pour logs)
     */
    String toString() const {
        String out;
        out.reserve(128);
        out += "[TinyBMS] ";
        out += "U=" + String(voltage, 2) + "V, ";
        out += "I=" + String(current, 1) + "A, ";
        out += "SOC=" + String(soc_percent, 1) + "%, ";
        out += "SOH=" + String(soh_percent, 1) + "%, ";
        out += "T=" + String(temperature / 10.0, 1) + "°C, ";
        out += "ΔV=" + String(cell_imbalance_mv) + "mV";
        return out;
    }

    void resetSnapshots() {
        register_count = 0;
    }

    bool appendSnapshot(uint16_t address,
                        TinyRegisterValueType type,
                        float /*scaled_value*/,
                        int32_t raw_value,
                        uint8_t raw_words) {
        if (register_count >= TINY_LIVEDATA_MAX_REGISTERS) {
            return false;
        }

        TinyRegisterSnapshot& snap = register_snapshots[register_count++];
        snap.address = address;
        snap.type = static_cast<uint8_t>(type);
        snap.raw_value = raw_value;
        snap.raw_word_count = raw_words;
        return true;
    }

    const TinyRegisterSnapshot* findSnapshot(uint16_t address) const {
        for (uint16_t i = 0; i < register_count; ++i) {
            if (register_snapshots[i].address == address) {
                return &register_snapshots[i];
            }
        }
        return nullptr;
    }

    size_t snapshotCount() const {
        return register_count;
    }

    const TinyRegisterSnapshot& snapshotAt(size_t index) const {
        return register_snapshots[index];
    }

    void applyField(TinyLiveDataField field, float scaled_value, int32_t raw_value) {
        switch (field) {
            case TinyLiveDataField::Voltage:
                voltage = scaled_value;
                break;
            case TinyLiveDataField::Current:
                current = scaled_value;
                break;
            case TinyLiveDataField::SocPercent:
                soc_percent = scaled_value;
                soc_raw = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::SohPercent:
                soh_percent = scaled_value;
                soh_raw = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::Temperature:
                temperature = static_cast<int16_t>(raw_value);
                break;
            case TinyLiveDataField::MinCellMv:
                min_cell_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::MaxCellMv:
                max_cell_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::BalancingBits:
                balancing_bits = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::MaxChargeCurrent:
                max_charge_current = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::MaxDischargeCurrent:
                max_discharge_current = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::OnlineStatus:
                online_status = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::NeedBalancing:
                // reserved
                break;
            case TinyLiveDataField::CellImbalanceMv:
                cell_imbalance_mv = static_cast<uint16_t>(raw_value);
                break;
            case TinyLiveDataField::None:
            default:
                break;
        }
    }

    void applyBinding(const TinyRegisterRuntimeBinding& binding,
                      int32_t raw_value,
                      float scaled_value) {
        applyField(binding.live_field, scaled_value, raw_value);
        appendSnapshot(binding.metadata_address,
                       binding.value_type,
                       scaled_value,
                       raw_value,
                       binding.register_count);
    }
};

// ====================================================================================
// OUTILS DE LOG (FACULTATIFS)
// ====================================================================================

#ifdef LOGGER_AVAILABLE
#include "logger.h"
extern Logger logger;

/**
 * @brief Macro pour journaliser un snapshot de données BMS
 * 
 * Exemple :
 * ```
 * TinyBMS_LiveData data;
 * LOG_LIVEDATA(data, LOG_DEBUG);
 * ```
 */
#define LOG_LIVEDATA(data, level) \
    do { \
        if (logger.getLevel() >= level) { \
            logger.log(level, (data).toString()); \
        } \
    } while (0)
#else
/**
 * @brief Stub vide si logger non disponible (compilation sans logs)
 */
#define LOG_LIVEDATA(data, level) do {} while (0)
#endif

#endif // SHARED_DATA_H