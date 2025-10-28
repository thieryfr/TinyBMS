#pragma once

#include <cstdint>
#include "uart/uart_channel.h"

namespace tinybms {

struct TransactionOptions {
    uint8_t attempt_count = 1;
    uint32_t retry_delay_ms = 0;
    uint32_t response_timeout_ms = 100;
};

struct DelayConfig {
    void (*delay_fn)(uint32_t delay_ms, void* context) = nullptr;
    void* context = nullptr;
};

enum class AttemptStatus {
    Success,
    Timeout,
    CrcMismatch,
    WriteError,
    ProtocolError
};

struct TransactionResult {
    bool success = false;
    AttemptStatus last_status = AttemptStatus::ProtocolError;
    uint32_t retries_performed = 0;
    uint32_t timeout_count = 0;
    uint32_t crc_error_count = 0;
    uint32_t write_error_count = 0;
};

TransactionResult readHoldingRegisters(IUartChannel& uart,
                                       uint16_t start_addr,
                                       uint16_t count,
                                       uint16_t* output,
                                       const TransactionOptions& options,
                                       const DelayConfig& delay = {});

} // namespace tinybms
