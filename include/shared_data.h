/**
 * @file shared_data.h
 * @brief Shared data structures for FreeRTOS tasks + Logging utilities
 */

#ifndef SHARED_DATA_H
#define SHARED_DATA_H

#include <Arduino.h>

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