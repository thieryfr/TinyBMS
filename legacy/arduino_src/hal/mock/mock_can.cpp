#include "hal/interfaces/ihal_can.h"

#include <deque>
#include <memory>
#include <vector>

namespace hal {

class MockCan : public IHalCan {
public:
    Status initialize(const CanConfig& config) override {
        config_ = config;
        stats_ = {};
        return Status::Ok;
    }

    Status transmit(const CanFrame& frame) override {
        tx_frames_.push_back(frame);
        stats_.tx_success++;
        return Status::Ok;
    }

    Status receive(CanFrame& frame, uint32_t timeout_ms) override {
        (void)timeout_ms;
        if (rx_frames_.empty()) {
            return Status::Timeout;
        }
        frame = rx_frames_.front();
        rx_frames_.pop_front();
        stats_.rx_success++;
        return Status::Ok;
    }

    Status configureFilters(const std::vector<CanFilterConfig>& filters) override {
        filters_ = filters;
        return Status::Ok;
    }

    CanStats getStats() const override { return stats_; }

    void resetStats() override { stats_ = {}; }

    void pushRx(const CanFrame& frame) { rx_frames_.push_back(frame); }
    const std::vector<CanFrame>& transmitted() const { return tx_frames_; }

private:
    CanConfig config_{};
    std::deque<CanFrame> rx_frames_;
    std::vector<CanFrame> tx_frames_;
    std::vector<CanFilterConfig> filters_;
    CanStats stats_{};
};

std::unique_ptr<IHalCan> createMockCan() {
    return std::make_unique<MockCan>();
}

} // namespace hal
