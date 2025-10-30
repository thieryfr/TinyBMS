/**
 * @file esp32_can_idf.h
 * @brief ESP-IDF native CAN (TWAI) HAL implementation header
 *
 * Phase 1: Fondations ESP-IDF
 */

#pragma once

#include "hal/interfaces/ihal_can.h"
#include "driver/twai.h"

namespace hal {

class ESP32CanIDF : public IHalCan {
public:
    ESP32CanIDF();
    ~ESP32CanIDF() override;

    // IHalCan interface implementation
    Status initialize(const CanConfig& config) override;
    Status transmit(const CanFrame& frame) override;
    Status receive(CanFrame& frame, uint32_t timeout_ms) override;
    Status configureFilters(const std::vector<CanFilterConfig>& filters) override;
    CanStats getStats() const override;
    void resetStats() override;

private:
    bool initialized_;
    CanStats stats_;
    CanConfig config_;

    twai_timing_config_t getBitrateConfig(uint32_t bitrate);
};

} // namespace hal
