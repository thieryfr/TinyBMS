/**
 * @file system_monitor.h
 * @brief System monitoring utilities for Phase 4
 * @version 1.0 - Task stack monitoring, heap tracking, performance metrics
 *
 * Provides runtime monitoring of:
 * - FreeRTOS task stack high water marks
 * - Heap memory usage and fragmentation
 * - Task CPU utilization
 * - System uptime and health
 */

#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace tinybms {
namespace monitor {

/**
 * @brief Task stack statistics
 */
struct TaskStackStats {
    const char* name;
    UBaseType_t stack_size;
    UBaseType_t high_water_mark;
    float usage_percent;
    TaskHandle_t handle;
};

/**
 * @brief System memory statistics
 */
struct SystemMemoryStats {
    size_t total_heap;
    size_t free_heap;
    size_t min_free_heap;
    size_t largest_free_block;
    size_t heap_fragmentation_percent;
};

/**
 * @brief System performance metrics
 */
struct SystemPerformanceMetrics {
    uint32_t uptime_ms;
    uint32_t uptime_hours;
    float cpu_load_percent;
    uint32_t task_switches;
    uint32_t context_switches_per_second;
};

/**
 * @brief Get stack statistics for a specific task
 *
 * @param handle Task handle (use NULL for current task)
 * @param stats Output structure for statistics
 * @return true if successful, false otherwise
 */
bool getTaskStackStats(TaskHandle_t handle, TaskStackStats& stats);

/**
 * @brief Get stack statistics for all critical tasks
 *
 * @param stats_array Array to fill with task statistics
 * @param max_tasks Maximum number of tasks to query
 * @return Number of tasks queried
 */
uint8_t getAllTaskStackStats(TaskStackStats* stats_array, uint8_t max_tasks);

/**
 * @brief Get current system memory statistics
 *
 * @param stats Output structure for memory statistics
 */
void getSystemMemoryStats(SystemMemoryStats& stats);

/**
 * @brief Get system performance metrics
 *
 * @param metrics Output structure for performance metrics
 */
void getSystemPerformanceMetrics(SystemPerformanceMetrics& metrics);

/**
 * @brief Print all task stack statistics to Serial
 *
 * @param log_level Minimum log level to print (LOG_INFO, LOG_DEBUG, etc.)
 */
void printAllTaskStackStats(uint8_t log_level = 3);  // LOG_INFO = 3

/**
 * @brief Print system memory statistics to Serial
 *
 * @param log_level Minimum log level to print
 */
void printSystemMemoryStats(uint8_t log_level = 3);

/**
 * @brief Print system performance metrics to Serial
 *
 * @param log_level Minimum log level to print
 */
void printSystemPerformanceMetrics(uint8_t log_level = 3);

/**
 * @brief Check if any task is at risk of stack overflow
 *
 * @param threshold_percent Threshold percentage (default 80%)
 * @return true if at least one task is at risk
 */
bool checkTaskStackHealth(float threshold_percent = 80.0f);

/**
 * @brief Check if system heap is healthy
 *
 * @param min_free_kb Minimum free heap in KB (default 150KB)
 * @return true if heap is healthy
 */
bool checkHeapHealth(size_t min_free_kb = 150);

} // namespace monitor
} // namespace tinybms
