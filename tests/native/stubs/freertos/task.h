#pragma once

#include "FreeRTOS.h"

namespace freertos_stub {
struct Task {
    TaskFunction_t function = nullptr;
    void* parameter = nullptr;
};
} // namespace freertos_stub

inline BaseType_t xTaskCreate(TaskFunction_t function,
                              const char*,
                              uint16_t,
                              void* parameter,
                              UBaseType_t,
                              TaskHandle_t* task_handle) {
    if (!function) {
        return pdFALSE;
    }
    auto* task = new freertos_stub::Task{};
    task->function = function;
    task->parameter = parameter;
    if (task_handle) {
        *task_handle = static_cast<TaskHandle_t>(task);
    }
    return pdPASS;
}

inline void vTaskDelete(TaskHandle_t handle) {
    delete static_cast<freertos_stub::Task*>(handle);
}

inline void taskYIELD() {}

