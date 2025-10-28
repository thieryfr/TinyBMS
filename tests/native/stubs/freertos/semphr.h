#pragma once

#include "FreeRTOS.h"

namespace freertos_stub {
struct Mutex {
    bool locked = false;
};
} // namespace freertos_stub

inline SemaphoreHandle_t xSemaphoreCreateMutex() {
    return static_cast<SemaphoreHandle_t>(new freertos_stub::Mutex{});
}

inline void vSemaphoreDelete(SemaphoreHandle_t handle) {
    delete static_cast<freertos_stub::Mutex*>(handle);
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t handle, TickType_t) {
    auto* mutex = static_cast<freertos_stub::Mutex*>(handle);
    if (!mutex) {
        return pdFALSE;
    }
    if (!mutex->locked) {
        mutex->locked = true;
        return pdTRUE;
    }
    // Allow recursive take for simplicity
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t handle) {
    auto* mutex = static_cast<freertos_stub::Mutex*>(handle);
    if (!mutex) {
        return pdFALSE;
    }
    mutex->locked = false;
    return pdTRUE;
}

