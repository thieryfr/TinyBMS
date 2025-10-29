#include "bridge_event_sink.h"

#include "event_bus.h"

namespace {
class EventBusBridgeEventSink final : public BridgeEventSink {
public:
    explicit EventBusBridgeEventSink(EventBus& bus) : bus_(&bus) {}

    bool isReady() const override {
        return bus_ != nullptr && bus_->isInitialized();
    }

    bool publishLiveData(const TinyBMS_LiveData& data, uint32_t source_id) override {
        return bus_ != nullptr && bus_->publishLiveData(data, source_id);
    }

    bool publishMqttRegister(const MqttRegisterEvent& data, uint32_t source_id) override {
        return bus_ != nullptr && bus_->publishMqttRegister(data, source_id);
    }

    bool publishAlarm(uint16_t alarm_code,
                      const char* message,
                      AlarmSeverity severity,
                      float value,
                      uint32_t source_id) override {
        return bus_ != nullptr && bus_->publishAlarm(alarm_code, message, severity, value, source_id);
    }

    bool publishStatus(const char* message, uint32_t source_id, StatusLevel level) override {
        return bus_ != nullptr && bus_->publishStatus(message, source_id, level);
    }

    bool publishCVLStateChange(uint8_t old_state,
                               uint8_t new_state,
                               float new_cvl_voltage,
                               float new_ccl_current,
                               float new_dcl_current,
                               uint32_t state_duration_ms,
                               uint32_t source_id) override {
        return bus_ != nullptr && bus_->publishCVLStateChange(old_state,
                                                              new_state,
                                                              new_cvl_voltage,
                                                              new_ccl_current,
                                                              new_dcl_current,
                                                              state_duration_ms,
                                                              source_id);
    }

    bool getLatestLiveData(TinyBMS_LiveData& data_out) const override {
        return bus_ != nullptr && bus_->getLatestLiveData(data_out);
    }

private:
    EventBus* bus_;
};
}  // namespace

BridgeEventSink& defaultBridgeEventSink() {
    static EventBusBridgeEventSink sink(EventBus::getInstance());
    return sink;
}
