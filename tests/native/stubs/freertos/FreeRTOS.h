#pragma once

#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned int;
using TickType_t = uint32_t;
using TaskHandle_t = void*;
using QueueHandle_t = void*;
using SemaphoreHandle_t = void*;

constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;

inline TickType_t pdMS_TO_TICKS(uint32_t ms) {
    return ms;
}

#define portYIELD_FROM_ISR()

typedef void (*TaskFunction_t)(void*);

