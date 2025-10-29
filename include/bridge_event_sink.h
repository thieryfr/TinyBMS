#pragma once

#include "event_types.h"

class EventBus;

class BridgeEventSink {
public:
    virtual ~BridgeEventSink() = default;

    virtual bool isReady() const = 0;
    virtual bool publishLiveData(const TinyBMS_LiveData& data, uint32_t source_id) = 0;
    virtual bool publishMqttRegister(const MqttRegisterEvent& data, uint32_t source_id) = 0;
    virtual bool publishAlarm(uint16_t alarm_code,
                              const char* message,
                              AlarmSeverity severity,
                              float value,
                              uint32_t source_id) = 0;
    virtual bool publishStatus(const char* message,
                               uint32_t source_id,
                               StatusLevel level) = 0;
    virtual bool publishCVLStateChange(uint8_t old_state,
                                       uint8_t new_state,
                                       float new_cvl_voltage,
                                       float new_ccl_current,
                                       float new_dcl_current,
                                       uint32_t state_duration_ms,
                                       uint32_t source_id) = 0;
    virtual bool getLatestLiveData(TinyBMS_LiveData& data_out) const = 0;
};

BridgeEventSink& defaultBridgeEventSink(EventBus& bus);
