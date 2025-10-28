#include <cassert>
#include <cmath>
#include "cvl_logic.h"

namespace {
CVLConfigSnapshot makeConfig() {
    CVLConfigSnapshot cfg;
    cfg.enabled = true;
    cfg.bulk_target_voltage_v = 58.4f;
    cfg.bulk_soc_threshold = 90.0f;
    cfg.transition_soc_threshold = 95.0f;
    cfg.float_soc_threshold = 98.0f;
    cfg.float_exit_soc = 95.0f;
    cfg.float_approach_offset_mv = 50.0f;
    cfg.float_offset_mv = 100.0f;
    cfg.minimum_ccl_in_float_a = 0.5f;
    cfg.imbalance_hold_threshold_mv = 120;
    cfg.imbalance_release_threshold_mv = 40;
    return cfg;
}

CVLInputs makeInputs(float soc_percent) {
    CVLInputs inputs;
    inputs.soc_percent = soc_percent;
    inputs.cell_imbalance_mv = 20;
    inputs.pack_voltage_v = 54.0f;
    inputs.base_ccl_limit_a = 50.0f;
    inputs.base_dcl_limit_a = 100.0f;
    return inputs;
}

void expectClose(float lhs, float rhs, float eps = 0.001f) {
    assert(std::fabs(lhs - rhs) <= eps);
}
}

int main() {
    CVLConfigSnapshot cfg = makeConfig();

    // Bulk state when SOC below bulk threshold
    CVLInputs inputs = makeInputs(80.0f);
    auto result = computeCvlLimits(inputs, cfg, CVL_BULK);
    assert(result.state == CVL_BULK);
    expectClose(result.cvl_voltage_v, 58.4f);
    expectClose(result.ccl_limit_a, 50.0f);
    expectClose(result.dcl_limit_a, 100.0f);

    // Transition state between bulk and transition thresholds
    inputs = makeInputs(92.0f);
    result = computeCvlLimits(inputs, cfg, CVL_BULK);
    assert(result.state == CVL_TRANSITION);

    // Float approach state once SOC crosses transition threshold
    inputs = makeInputs(96.0f);
    result = computeCvlLimits(inputs, cfg, CVL_TRANSITION);
    assert(result.state == CVL_FLOAT_APPROACH);
    expectClose(result.cvl_voltage_v, 58.35f, 0.002f);

    // Float state when SOC crosses float threshold and CCL limited
    inputs = makeInputs(99.0f);
    result = computeCvlLimits(inputs, cfg, CVL_FLOAT_APPROACH);
    assert(result.state == CVL_FLOAT);
    expectClose(result.ccl_limit_a, 0.5f);
    expectClose(result.cvl_voltage_v, 58.3f, 0.002f);

    // Remain in float while SOC above exit threshold
    inputs = makeInputs(96.0f);
    result = computeCvlLimits(inputs, cfg, CVL_FLOAT);
    assert(result.state == CVL_FLOAT);

    // Exit float once SOC drops below exit threshold
    inputs = makeInputs(90.0f);
    result = computeCvlLimits(inputs, cfg, CVL_FLOAT);
    assert(result.state == CVL_TRANSITION || result.state == CVL_BULK);

    // Enter imbalance hold when imbalance exceeds threshold
    inputs = makeInputs(97.0f);
    inputs.cell_imbalance_mv = 150;
    result = computeCvlLimits(inputs, cfg, CVL_FLOAT_APPROACH);
    assert(result.state == CVL_IMBALANCE_HOLD);
    expectClose(result.ccl_limit_a, 0.5f);

    // Leave imbalance hold once imbalance drops below release threshold
    inputs = makeInputs(85.0f);
    inputs.cell_imbalance_mv = 20;
    result = computeCvlLimits(inputs, cfg, CVL_IMBALANCE_HOLD);
    assert(result.state == CVL_BULK);

    // Disabled algorithm should pass-through limits without forcing float
    cfg.enabled = false;
    inputs = makeInputs(99.0f);
    result = computeCvlLimits(inputs, cfg, CVL_FLOAT);
    assert(result.state == CVL_BULK);
    expectClose(result.ccl_limit_a, 50.0f);
    expectClose(result.dcl_limit_a, 100.0f);

    return 0;
}
