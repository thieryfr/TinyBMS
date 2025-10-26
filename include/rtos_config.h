/**
 * @file rtos_config.h
 * @brief FreeRTOS configuration constants for TinyBMS to Victron Bridge
 * 
 * NOTE: Do NOT add logging or includes here.
 * This header must remain lightweight and self-contained because it is used
 * across all tasks at compile time.
 */
#ifndef RTOS_CONFIG_H
#define RTOS_CONFIG_H

// Queue Sizes
#define LIVE_DATA_QUEUE_SIZE 1 // Single entry - latest live data only

// Task Stack Sizes (bytes)
#define TASK_DEFAULT_STACK_SIZE 4096

// Task Priorities
#define TASK_HIGH_PRIORITY 2   // Critical tasks: UART, CAN
#define TASK_NORMAL_PRIORITY 1 // Non-critical tasks: Web, CVL, etc.

// Timing Intervals (ms)
#define WEBSOCKET_UPDATE_INTERVAL_MS 1000
#define UART_POLL_INTERVAL_MS        100
#define PGN_UPDATE_INTERVAL_MS       1000
#define CVL_UPDATE_INTERVAL_MS       20000

// Watchdog Timing (ms)
#define WATCHDOG_DEFAULT_TIMEOUT     5000
#define WATCHDOG_MIN_TIMEOUT         1000
#define WATCHDOG_MAX_TIMEOUT         30000
#define WATCHDOG_MIN_FEED_INTERVAL   100

#endif // RTOS_CONFIG_H