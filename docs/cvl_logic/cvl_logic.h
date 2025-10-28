#pragma once

#include <cstdint>
#include "cvl_types.h"

struct CVLInputs {
    float soc_percent = 0.0f;
    uint16_t cell_imbalance_mv = 0;
    float pack_voltage_v = 0.0f;
    float base_ccl_limit_a = 0.0f;
    float base_dcl_limit_a = 0.0f;
    
    // NOUVELLES ENTREES POUR LE P-CONTROL DE PROTECTION
    float max_cell_voltage_v = 0.0f; 
    uint16_t series_cell_count = 0; 
};

struct CVLConfigSnapshot {
    bool enabled = true;
    float bulk_soc_threshold = 90.0f;
    float transition_soc_threshold = 95.0f;
    float float_soc_threshold = 98.0f;
    float float_exit_soc = 95.0f;
    float float_approach_offset_mv = 50.0f;
    float float_offset_mv = 100.0f;
    float minimum_ccl_in_float_a = 5.0f;
    uint16_t imbalance_hold_threshold_mv = 100;
    uint16_t imbalance_release_threshold_mv = 50;
    float bulk_target_voltage_v = 0.0f;
};

struct CVLComputationResult {
    CVLState state = CVL_BULK;
    float cvl_voltage_v = 0.0f;
    float ccl_limit_a = 0.0f;
    float dcl_limit_a = 0.0f;
    bool imbalance_hold_active = false;
};

CVLComputationResult computeCvlLimits(const CVLInputs& input,
                                      const CVLConfigSnapshot& config,
                                      CVLState previous_state);
