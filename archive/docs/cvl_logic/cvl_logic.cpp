#include "cvl_logic.h"

#include <algorithm>
#include <cmath>

namespace {

// Constantes ajustées pour vos cellules LiFePO4 3.65V Max.
const float VCELL_CUTOFF_V = 3.65f;         // V (Tension maximale absolue de la cellule)
const float VCELL_SAFETY_THRESHOLD = 3.50f; // V (Seuil de démarrage de la limitation du CVL)
const float KP_GAIN = 150.0f;               // Gain P : Plus agressif pour une réaction rapide
const float VCELL_MIN_FLOAT = 3.20f;        // V (Tension minimale pour éviter la décharge)

float clampNonNegative(float value) {
    return value < 0.0f ? 0.0f : value;
}

/**
 * @brief Calcule la limite de tension de charge (CVL) pour le P-Control de protection cellulaire.
 * Le CVL est abaissé si max_cell_voltage_v > VCELL_SAFETY_THRESHOLD.
 */
float compute_cell_protection_cvl(float max_cell_voltage_v, uint16_t n_cells) {
    if (n_cells == 0) {
        return 0.0f; // Sécurité
    }
    
    // 1. Calcul des limites du pack
    float V_absmax = VCELL_CUTOFF_V * (float)n_cells; 
    float V_min_limit = VCELL_MIN_FLOAT * (float)n_cells;

    float CVL_calculated_V;

    if (max_cell_voltage_v <= VCELL_SAFETY_THRESHOLD) {
        // Condition normale : CVL au maximum permis (basé sur la tension de coupure de la cellule).
        CVL_calculated_V = V_absmax;
    } else {
        // Condition critique : Réduction proportionnelle du CVL.
        float error_V = max_cell_voltage_v - VCELL_SAFETY_THRESHOLD;
        float V_reduction = KP_GAIN * error_V;
        
        // CVL = V_absmax - (Réduction P)
        CVL_calculated_V = V_absmax - V_reduction;
    }

    // 2. Application des contraintes de sécurité
    // Le CVL ne doit pas dépasser V_absmax
    CVL_calculated_V = std::min(CVL_calculated_V, V_absmax); 
    
    // Le CVL ne doit pas descendre sous la limite minimale de flottement pour éviter la décharge
    CVL_calculated_V = std::max(CVL_calculated_V, V_min_limit);

    return CVL_calculated_V;
}

} // namespace

CVLComputationResult computeCvlLimits(const CVLInputs& input,
                                      const CVLConfigSnapshot& config,
                                      CVLState previous_state) {
    CVLComputationResult result{};

    // Default passthrough when algorithm is disabled
    if (!config.enabled) {
        result.state = CVL_BULK;
        result.cvl_voltage_v = config.bulk_target_voltage_v;
        result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
        result.dcl_limit_a = clampNonNegative(input.base_dcl_limit_a);
        result.imbalance_hold_active = false;
        return result;
    }

    const float bulk_target = std::max(config.bulk_target_voltage_v, 0.0f);
    float float_approach = bulk_target - (config.float_approach_offset_mv / 1000.0f);
    float float_voltage = bulk_target - (config.float_offset_mv / 1000.0f);
    float_approach = std::max(float_approach, 0.0f);
    float_voltage = std::max(float_voltage, 0.0f);

    if (float_voltage > float_approach) {
        std::swap(float_voltage, float_approach);
    }

    float soc = input.soc_percent;
    CVLState state = previous_state;
    
    // State machine logic
    // Handle Imbalance Hold transitions
    if (input.cell_imbalance_mv > config.imbalance_hold_threshold_mv) {
        state = CVL_IMBALANCE_HOLD;
    } else if (state == CVL_IMBALANCE_HOLD && input.cell_imbalance_mv < config.imbalance_release_threshold_mv) {
        // Revert to SOC-based state
        if (soc >= config.float_soc_threshold) {
            state = CVL_FLOAT;
        } else if (soc >= config.transition_soc_threshold) {
            state = CVL_FLOAT_APPROACH;
        } else if (soc >= config.bulk_soc_threshold) {
            state = CVL_TRANSITION;
        } else {
            state = CVL_BULK;
        }
    } else {
        // SOC-based state transitions
        if (soc >= config.float_soc_threshold) {
            state = CVL_FLOAT;
        } else if (soc >= config.transition_soc_threshold) {
            state = CVL_FLOAT_APPROACH;
        } else if (soc >= config.bulk_soc_threshold) {
            state = CVL_TRANSITION;
        }
        // ... Logique de sortie du mode FLOAT
        if (state == CVL_FLOAT && soc <= config.float_exit_soc) {
            state = CVL_FLOAT_APPROACH;
        }
        
        if (state == CVL_FLOAT_APPROACH && previous_state == CVL_FLOAT_APPROACH &&
            soc + 0.25f < config.transition_soc_threshold) {
            state = CVL_TRANSITION;
        }
    }

    result.state = state;
    result.dcl_limit_a = clampNonNegative(input.base_dcl_limit_a);
    result.imbalance_hold_active = (state == CVL_IMBALANCE_HOLD);

    switch (state) {
        case CVL_BULK:
        case CVL_TRANSITION:
            result.cvl_voltage_v = bulk_target;
            result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            break;

        case CVL_FLOAT_APPROACH:
            result.cvl_voltage_v = float_approach;
            result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            break;

        case CVL_FLOAT: {
            result.cvl_voltage_v = float_voltage;
            const float min_ccl = std::max(config.minimum_ccl_in_float_a, 0.0f);
            if (min_ccl > 0.0f) {
                result.ccl_limit_a = std::min(clampNonNegative(input.base_ccl_limit_a), min_ccl);
            } else {
                result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            }
            break;
        }

        case CVL_IMBALANCE_HOLD:
            // Pour le mode 'Hold', on calcule V_min_limit_pack 
            float V_min_limit_pack = VCELL_MIN_FLOAT * (float)input.series_cell_count;
            // Le CVL est abaissé de 1.0 V par rapport au bulk target ou au V_min_limit_pack
            result.cvl_voltage_v = std::max(bulk_target - 1.0f, V_min_limit_pack); 
            result.ccl_limit_a = clampNonNegative(input.base_ccl_limit_a);
            break;
    }


    // ----------------------------------------------------------------------
    // ETAPE FINALE : Application de la limite de tension de protection Vcell_max
    // Cette limite écrase la limite du SOC si elle est plus restrictive (plus basse)
    // ----------------------------------------------------------------------
    float protection_cvl = compute_cell_protection_cvl(
        input.max_cell_voltage_v, 
        input.series_cell_count
    );

    // Le CVL final est la valeur la plus restrictive.
    result.cvl_voltage_v = std::min(result.cvl_voltage_v, protection_cvl);
    
    return result;
}
