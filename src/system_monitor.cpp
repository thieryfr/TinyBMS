/**
 * @file system_monitor.cpp
 * @brief System monitoring utilities implementation
 * @version 1.0 - Phase 4 optimizations
 */

#include "system_monitor.h"
#include "logger.h"
#include "rtos_tasks.h"
#include <esp_heap_caps.h>
#include <esp_system.h>

extern Logger logger;

// External task handles
extern TaskHandle_t webServerTaskHandle;
extern TaskHandle_t websocketTaskHandle;
extern TaskHandle_t watchdogTaskHandle;
extern TaskHandle_t mqttTaskHandle;

namespace tinybms {
namespace monitor {

bool getTaskStackStats(TaskHandle_t handle, TaskStackStats& stats) {
    if (!handle) {
        handle = xTaskGetCurrentTaskHandle();
    }

    if (!handle) {
        return false;
    }

    stats.handle = handle;
    stats.name = pcTaskGetName(handle);
    stats.high_water_mark = uxTaskGetStackHighWaterMark(handle);

    // Note: Getting exact stack size requires task creation parameters
    // which we don't have access to. Using a heuristic based on common sizes.
    // This should be improved with actual stack size tracking.
    stats.stack_size = 8192;  // Default assumption

    if (stats.high_water_mark > 0 && stats.stack_size > 0) {
        UBaseType_t used = stats.stack_size - (stats.high_water_mark * sizeof(StackType_t));
        stats.usage_percent = (float)used / (float)stats.stack_size * 100.0f;
    } else {
        stats.usage_percent = 0.0f;
    }

    return true;
}

uint8_t getAllTaskStackStats(TaskStackStats* stats_array, uint8_t max_tasks) {
    if (!stats_array || max_tasks == 0) {
        return 0;
    }

    uint8_t count = 0;

    // Query known task handles
    TaskHandle_t handles[] = {
        webServerTaskHandle,
        websocketTaskHandle,
        watchdogTaskHandle,
        mqttTaskHandle
    };

    const char* names[] = {
        "WebServer",
        "WebSocket",
        "Watchdog",
        "MQTT"
    };

    for (uint8_t i = 0; i < 4 && count < max_tasks; i++) {
        if (handles[i] != NULL) {
            if (getTaskStackStats(handles[i], stats_array[count])) {
                count++;
            }
        }
    }

    return count;
}

void getSystemMemoryStats(SystemMemoryStats& stats) {
    stats.free_heap = esp_get_free_heap_size();
    stats.min_free_heap = esp_get_minimum_free_heap_size();
    stats.largest_free_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    stats.total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);

    // Calculate fragmentation
    if (stats.free_heap > 0) {
        stats.heap_fragmentation_percent =
            100.0f * (1.0f - ((float)stats.largest_free_block / (float)stats.free_heap));
    } else {
        stats.heap_fragmentation_percent = 100.0f;
    }
}

void getSystemPerformanceMetrics(SystemPerformanceMetrics& metrics) {
    metrics.uptime_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;
    metrics.uptime_hours = metrics.uptime_ms / (3600 * 1000);

    // CPU load estimation would require more complex tracking
    // For now, provide placeholder
    metrics.cpu_load_percent = 0.0f;
    metrics.task_switches = 0;
    metrics.context_switches_per_second = 0;
}

void printAllTaskStackStats(uint8_t log_level) {
    TaskStackStats stats[10];
    uint8_t count = getAllTaskStackStats(stats, 10);

    logger.log((LogLevel)log_level, "=== Task Stack Statistics ===");

    for (uint8_t i = 0; i < count; i++) {
        String msg = String(stats[i].name) + ": "
                   + String(stats[i].high_water_mark * sizeof(StackType_t)) + "B free, "
                   + String(stats[i].usage_percent, 1) + "% used";
        logger.log((LogLevel)log_level, msg);
    }
}

void printSystemMemoryStats(uint8_t log_level) {
    SystemMemoryStats stats;
    getSystemMemoryStats(stats);

    logger.log((LogLevel)log_level, "=== System Memory Statistics ===");
    logger.log((LogLevel)log_level, "Free Heap: " + String(stats.free_heap / 1024) + " KB");
    logger.log((LogLevel)log_level, "Min Free Heap: " + String(stats.min_free_heap / 1024) + " KB");
    logger.log((LogLevel)log_level, "Largest Block: " + String(stats.largest_free_block / 1024) + " KB");
    logger.log((LogLevel)log_level, "Fragmentation: " + String(stats.heap_fragmentation_percent, 1) + "%");
}

void printSystemPerformanceMetrics(uint8_t log_level) {
    SystemPerformanceMetrics metrics;
    getSystemPerformanceMetrics(metrics);

    logger.log((LogLevel)log_level, "=== System Performance Metrics ===");
    logger.log((LogLevel)log_level, "Uptime: " + String(metrics.uptime_hours) + "h "
                                    + String((metrics.uptime_ms / 60000) % 60) + "m");
}

bool checkTaskStackHealth(float threshold_percent) {
    TaskStackStats stats[10];
    uint8_t count = getAllTaskStackStats(stats, 10);

    for (uint8_t i = 0; i < count; i++) {
        if (stats[i].usage_percent >= threshold_percent) {
            logger.log(LOG_WARNING,
                String("Task '") + stats[i].name
                + "' stack usage high: " + String(stats[i].usage_percent, 1) + "%");
            return false;
        }
    }

    return true;
}

bool checkHeapHealth(size_t min_free_kb) {
    SystemMemoryStats stats;
    getSystemMemoryStats(stats);

    size_t min_free_bytes = min_free_kb * 1024;

    if (stats.free_heap < min_free_bytes) {
        logger.log(LOG_WARNING,
            String("Low heap warning: ") + String(stats.free_heap / 1024)
            + " KB free (min: " + String(min_free_kb) + " KB)");
        return false;
    }

    return true;
}

} // namespace monitor
} // namespace tinybms
