/**
 * @file event_bus_config.h
 * @brief Configuration parameters for the Event Bus system
 *
 * This file contains all tunable parameters for the Event Bus architecture.
 * Adjust these values based on your system requirements and available resources.
 */

#ifndef EVENT_BUS_CONFIG_H
#define EVENT_BUS_CONFIG_H

// ====================================================================================
// QUEUE CONFIGURATION
// ====================================================================================

/**
 * @brief Size of the event queue (number of events)
 *
 * This queue holds pending events waiting to be dispatched to subscribers.
 * Larger values provide more buffering but consume more RAM.
 *
 * Calculation:
 * - Each event: ~200 bytes
 * - Queue overhead: ~80 bytes
 * - Total RAM: (EVENT_BUS_QUEUE_SIZE × 200) + 80 bytes
 *
 * Example: 20 events = ~4080 bytes
 *
 * Recommendation:
 * - Small systems (ESP32): 10-20 events
 * - Medium systems: 20-50 events
 * - Large systems: 50-100 events
 */
#define EVENT_BUS_QUEUE_SIZE          20

// ====================================================================================
// SUBSCRIBER CONFIGURATION
// ====================================================================================

/**
 * @brief Maximum number of subscribers (total across all event types)
 *
 * This limits the total number of subscriptions that can be registered.
 * Each subscription consumes ~24 bytes of RAM.
 *
 * Example: 32 subscribers × 24 bytes = 768 bytes
 *
 * Recommendation:
 * - Estimate: 2-3 subscribers per event type
 * - For 20 event types: 40-60 subscribers
 */
#define EVENT_BUS_MAX_SUBSCRIBERS     50

/**
 * @brief Maximum number of subscribers per event type
 *
 * This prevents a single event type from monopolizing all subscriber slots.
 *
 * Recommendation:
 * - Typical value: 5-10 subscribers per event type
 */
#define EVENT_BUS_MAX_SUBSCRIBERS_PER_TYPE  10

// ====================================================================================
// DISPATCH TASK CONFIGURATION
// ====================================================================================

/**
 * @brief Stack size for the dispatch task (bytes)
 *
 * The dispatch task runs callbacks when events are published.
 * This stack must be large enough to handle:
 * - FreeRTOS overhead (~200 bytes)
 * - Event processing (~500 bytes)
 * - Subscriber callbacks (varies)
 *
 * Recommendation:
 * - Minimum: 2048 bytes
 * - Typical: 4096 bytes
 * - Large callbacks: 8192 bytes
 */
#define EVENT_BUS_TASK_STACK_SIZE     4096

/**
 * @brief Priority for the dispatch task
 *
 * This task dispatches events to subscribers. It should run at a priority
 * between high-priority producers (UART, CAN) and normal-priority consumers.
 *
 * Priority levels:
 * - UART Task: Priority 2 (HIGH)
 * - CAN Task: Priority 2 (HIGH)
 * - Event Bus Dispatch: Priority 2 (HIGH) - same as producers
 * - WebSocket Task: Priority 1 (NORMAL)
 * - CVL Task: Priority 1 (NORMAL)
 *
 * Recommendation:
 * - Set to 2 (HIGH) to ensure timely event delivery
 * - Can be lowered to 1 (NORMAL) if latency is acceptable
 */
#define EVENT_BUS_TASK_PRIORITY       2  // HIGH priority (same as UART/CAN)

/**
 * @brief Dispatch task polling interval (milliseconds)
 *
 * How often the dispatch task checks for new events in the queue.
 * Lower values reduce latency but increase CPU usage.
 *
 * Recommendation:
 * - 1-10 ms for low latency
 * - 10-50 ms for balanced performance
 * - 50-100 ms for low CPU usage
 *
 * Note: This is a maximum wait time. The task will wake up immediately
 * when a new event is published (using FreeRTOS queue notifications).
 */
#define EVENT_BUS_DISPATCH_INTERVAL_MS  10

// ====================================================================================
// SYNCHRONIZATION CONFIGURATION
// ====================================================================================

/**
 * @brief Timeout for acquiring the Event Bus mutex (milliseconds)
 *
 * When multiple tasks try to publish events or subscribe simultaneously,
 * they must acquire a mutex. This timeout prevents deadlocks.
 *
 * Recommendation:
 * - 50-100 ms for normal operation
 * - 200-500 ms for systems with many tasks
 */
#define EVENT_BUS_MUTEX_TIMEOUT_MS    100

/**
 * @brief Timeout for publishing events from ISR (ticks)
 *
 * When publishing from an interrupt service routine, this timeout
 * determines how long to wait if the queue is full.
 *
 * Recommendation:
 * - 0 (do not wait, return immediately)
 * - 1-5 ticks (short wait acceptable)
 */
#define EVENT_BUS_ISR_TIMEOUT_TICKS   0

// ====================================================================================
// CACHE CONFIGURATION
// ====================================================================================

/**
 * @brief Enable caching of latest events
 *
 * When enabled, the Event Bus maintains a cache of the latest event for each type.
 * This allows subscribers to call getLatest() to retrieve the most recent event
 * without waiting for a new publish (similar to xQueuePeek() behavior).
 *
 * RAM cost: EVENT_TYPE_COUNT × sizeof(BusEvent) = 64 × 200 = ~12.8 KB
 *
 * Recommendation:
 * - Enable (1) if you need xQueuePeek()-like behavior
 * - Disable (0) to save RAM if not needed
 */
#define EVENT_BUS_CACHE_ENABLED       1

// ====================================================================================
// STATISTICS AND MONITORING
// ====================================================================================

/**
 * @brief Enable statistics tracking
 *
 * When enabled, the Event Bus tracks:
 * - Total events published
 * - Total events dispatched
 * - Queue overruns (events dropped due to full queue)
 * - Dispatch errors
 * - Per-subscriber call counts
 *
 * RAM cost: ~64 bytes + 4 bytes per subscriber
 *
 * Recommendation:
 * - Enable (1) during development and debugging
 * - Can be disabled (0) in production to save RAM
 */
#define EVENT_BUS_STATS_ENABLED       1

/**
 * @brief Enable per-subscriber statistics
 *
 * When enabled, the Event Bus tracks the number of times each
 * subscriber callback has been called.
 *
 * RAM cost: 4 bytes per subscriber
 *
 * Recommendation:
 * - Enable (1) for detailed debugging
 * - Disable (0) in production
 */
#define EVENT_BUS_PER_SUBSCRIBER_STATS  1

// ====================================================================================
// DEBUG AND LOGGING
// ====================================================================================

/**
 * @brief Enable debug logging
 *
 * When enabled, the Event Bus logs:
 * - Event publications
 * - Event dispatches
 * - Subscriber registrations
 * - Errors and warnings
 *
 * Note: Requires LOGGER_AVAILABLE to be defined
 *
 * Recommendation:
 * - Enable (1) during development
 * - Disable (0) in production to reduce log spam
 */
#define EVENT_BUS_DEBUG_ENABLED       1

/**
 * @brief Log level for Event Bus messages
 *
 * Controls the verbosity of Event Bus logging:
 * - 0 = LOG_ERROR (only errors)
 * - 1 = LOG_WARNING (warnings + errors)
 * - 2 = LOG_INFO (normal operation)
 * - 3 = LOG_DEBUG (detailed debugging)
 *
 * Recommendation:
 * - Development: 3 (LOG_DEBUG)
 * - Production: 1 (LOG_WARNING)
 */
#define EVENT_BUS_LOG_LEVEL           2  // LOG_INFO

/**
 * @brief Log event publications
 *
 * When enabled, every published event is logged.
 * Warning: Can generate a lot of log spam (10+ events/second)
 *
 * Recommendation:
 * - Enable (1) only for debugging specific issues
 * - Disable (0) for normal operation
 */
#define EVENT_BUS_LOG_PUBLICATIONS    0

/**
 * @brief Log event dispatches
 *
 * When enabled, every event dispatch to a subscriber is logged.
 * Warning: Can generate even more log spam than publications
 *
 * Recommendation:
 * - Enable (1) only for debugging subscriber issues
 * - Disable (0) for normal operation
 */
#define EVENT_BUS_LOG_DISPATCHES      0

// ====================================================================================
// SAFETY AND VALIDATION
// ====================================================================================

/**
 * @brief Enable data size validation
 *
 * When enabled, the Event Bus validates that event.data_size matches
 * the expected size for the event type. This catches programming errors
 * but adds a small overhead.
 *
 * Recommendation:
 * - Enable (1) during development
 * - Can be disabled (0) in production for slight performance gain
 */
#define EVENT_BUS_VALIDATE_DATA_SIZE  1

/**
 * @brief Enable null callback check
 *
 * When enabled, the Event Bus checks that subscriber callbacks are not null
 * before calling them. This catches programming errors but adds overhead.
 *
 * Recommendation:
 * - Enable (1) always (safety first)
 */
#define EVENT_BUS_CHECK_NULL_CALLBACKS  1

/**
 * @brief Maximum callback execution time (milliseconds)
 *
 * If a subscriber callback takes longer than this, a warning is logged.
 * This helps identify slow callbacks that could block event processing.
 *
 * Set to 0 to disable timing checks.
 *
 * Recommendation:
 * - 10-50 ms for typical systems
 * - 0 (disabled) if timing checks cause issues
 */
#define EVENT_BUS_MAX_CALLBACK_TIME_MS  50

// ====================================================================================
// MEMORY ESTIMATION
// ====================================================================================

/*
 * RAM Usage Estimation:
 *
 * Event Queue:
 *   - Queue structure: ~80 bytes
 *   - Queue storage: EVENT_BUS_QUEUE_SIZE × sizeof(BusEvent) = 20 × 200 = 4000 bytes
 *   - Total: ~4080 bytes
 *
 * Subscriber Registry:
 *   - Vector storage: EVENT_BUS_MAX_SUBSCRIBERS × 24 = 50 × 24 = 1200 bytes
 *
 * Event Cache (if enabled):
 *   - Cache storage: EVENT_TYPE_COUNT × sizeof(BusEvent) = 64 × 200 = 12800 bytes
 *
 * Statistics (if enabled):
 *   - Stats structure: ~64 bytes
 *   - Per-subscriber stats: EVENT_BUS_MAX_SUBSCRIBERS × 4 = 50 × 4 = 200 bytes
 *   - Total: ~264 bytes
 *
 * Mutexes and Handles:
 *   - bus_mutex_: ~80 bytes
 *   - dispatch_task_handle_: ~4 bytes
 *   - Total: ~84 bytes
 *
 * Task Stack:
 *   - Stack: EVENT_BUS_TASK_STACK_SIZE = 4096 bytes
 *
 * TOTAL RAM USAGE:
 *   - With cache: ~22.5 KB
 *   - Without cache: ~9.7 KB
 *
 * ESP32 has ~320 KB of RAM, so this is 7% (with cache) or 3% (without cache).
 */

#endif // EVENT_BUS_CONFIG_H
