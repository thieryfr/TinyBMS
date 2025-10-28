#pragma once

#include "FreeRTOS.h"
#include <cstring>
#include <deque>
#include <vector>

namespace freertos_stub {
struct Queue {
    size_t item_size;
    size_t max_items;
    std::deque<std::vector<uint8_t>> items;
};
} // namespace freertos_stub

inline QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size) {
    auto* queue = new freertos_stub::Queue{};
    queue->item_size = item_size;
    queue->max_items = queue_length;
    return static_cast<QueueHandle_t>(queue);
}

inline void vQueueDelete(QueueHandle_t handle) {
    delete static_cast<freertos_stub::Queue*>(handle);
}

inline BaseType_t xQueueSend(QueueHandle_t handle, const void* item, TickType_t) {
    auto* queue = static_cast<freertos_stub::Queue*>(handle);
    if (!queue) {
        return pdFALSE;
    }
    if (queue->items.size() >= queue->max_items) {
        return pdFALSE;
    }
    std::vector<uint8_t> buffer(queue->item_size, 0);
    if (item) {
        std::memcpy(buffer.data(), item, queue->item_size);
    }
    queue->items.push_back(std::move(buffer));
    return pdTRUE;
}

inline BaseType_t xQueueSendFromISR(QueueHandle_t handle, const void* item, BaseType_t* higher_priority_task_woken) {
    if (higher_priority_task_woken) {
        *higher_priority_task_woken = pdFALSE;
    }
    return xQueueSend(handle, item, 0);
}

inline BaseType_t xQueueReceive(QueueHandle_t handle, void* buffer, TickType_t) {
    auto* queue = static_cast<freertos_stub::Queue*>(handle);
    if (!queue || queue->items.empty()) {
        return pdFALSE;
    }
    auto data = std::move(queue->items.front());
    queue->items.pop_front();
    if (buffer) {
        std::memcpy(buffer, data.data(), queue->item_size);
    }
    return pdTRUE;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle) {
    auto* queue = static_cast<freertos_stub::Queue*>(handle);
    if (!queue) {
        return 0;
    }
    return static_cast<UBaseType_t>(queue->items.size());
}

