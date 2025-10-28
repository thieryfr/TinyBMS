/**
 * @file event_bus.h
 * @brief Central Event Bus for publish/subscribe architecture
 *
 * This file defines the EventBus singleton class that implements a central
 * publish/subscribe messaging system for decoupling system components.
 *
 * Key features:
 * - Publish/subscribe pattern with event filtering
 * - FreeRTOS-based queue and task for event dispatching
 * - Event caching for getLatest() functionality (xQueuePeek-like)
 * - Statistics and monitoring
 * - Thread-safe operation with mutex protection
 *
 * Usage example:
 * @code
 * // Publisher (UART Task)
 * TinyBMS_LiveData data;
 * eventBus.publishLiveData(data, SOURCE_ID_UART);
 *
 * // Subscriber (CAN Task)
 * void onLiveDataUpdate(const BusEvent& event, void* user_data) {
 *     const TinyBMS_LiveData& data = event.data.live_data;
 *     // Process data
 * }
 * eventBus.subscribe(EVENT_LIVE_DATA_UPDATE, onLiveDataUpdate);
 * @endcode
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <vector>
#include "event_types.h"
#include "event_bus_config.h"

// ====================================================================================
// CALLBACK TYPE
// ====================================================================================

/**
 * @brief Callback function type for event subscribers
 *
 * @param event The event that was published (const reference, zero-copy)
 * @param user_data Optional user data passed during subscription
 */
typedef void (*EventCallback)(const BusEvent& event, void* user_data);

// ====================================================================================
// EVENT BUS CLASS (SINGLETON)
// ====================================================================================

/**
 * @class EventBus
 * @brief Central event bus for publish/subscribe messaging
 *
 * This class implements a singleton event bus that allows components to:
 * - Publish events without knowing who will consume them
 * - Subscribe to specific event types
 * - Retrieve the latest event of each type (cached)
 * - Monitor bus statistics and health
 *
 * The EventBus uses:
 * - FreeRTOS queue for event buffering
 * - FreeRTOS task for event dispatching
 * - Mutex for thread-safe operation
 * - Vector for subscriber registry
 * - Array for event caching
 */
class EventBus {
public:
    // ====================================================================================
    // SINGLETON ACCESS
    // ====================================================================================

    /**
     * @brief Get the singleton instance of the EventBus
     * @return Reference to the EventBus instance
     */
    static EventBus& getInstance();

    /**
     * @brief Initialize the Event Bus
     *
     * This must be called once during system startup (in setup()).
     * It creates the FreeRTOS queue, mutex, and dispatch task.
     *
     * @param queue_size Number of events the queue can hold (default from config)
     * @return true if initialization succeeded, false otherwise
     */
    bool begin(size_t queue_size = EVENT_BUS_QUEUE_SIZE);

    /**
     * @brief Check if the Event Bus is initialized
     * @return true if begin() was called successfully
     */
    bool isInitialized() const { return initialized_; }

    // ====================================================================================
    // PUBLISHING EVENTS
    // ====================================================================================

    /**
     * @brief Publish a generic event
     *
     * This is the low-level publish function. For common event types,
     * use the convenience functions below (publishLiveData, publishAlarm, etc.)
     *
     * @param type Event type
     * @param data Pointer to event data (will be copied)
     * @param data_size Size of event data in bytes
     * @param source_id ID of the publishing component (EventSource enum)
     * @param from_isr true if called from an interrupt service routine
     * @return true if event was published, false if queue is full
     */
    bool publish(EventType type, const void* data, size_t data_size,
                 uint32_t source_id = SOURCE_ID_UNKNOWN, bool from_isr = false);

    /**
     * @brief Publish a live data update event
     *
     * Convenience function for EVENT_LIVE_DATA_UPDATE.
     *
     * @param data TinyBMS live data
     * @param source_id Source component (default: UART)
     * @return true if published successfully
     */
    bool publishLiveData(const TinyBMS_LiveData& data,
                         uint32_t source_id = SOURCE_ID_UART);

    /**
     * @brief Publish a register snapshot ready for MQTT formatting
     *
     * Convenience function for EVENT_MQTT_REGISTER_VALUE.
     *
     * @param data Register payload constructed from TinyBMS bindings
     * @param source_id Source component (default: UART)
     * @return true if published successfully
     */
    bool publishMqttRegister(const MqttRegisterEvent& data,
                              uint32_t source_id = SOURCE_ID_UART,
                              bool from_isr = false);

    /**
     * @brief Publish an alarm event
     *
     * Convenience function for EVENT_ALARM_RAISED.
     *
     * @param alarm_code Alarm code (AlarmCode enum)
     * @param message Human-readable message (max 63 chars)
     * @param severity Alarm severity (default: ERROR)
     * @param value Optional value that triggered the alarm
     * @param source_id Source component (default: SYSTEM)
     * @return true if published successfully
     */
    bool publishAlarm(uint16_t alarm_code, const char* message,
                      AlarmSeverity severity = ALARM_SEVERITY_ERROR,
                      float value = 0.0,
                      uint32_t source_id = SOURCE_ID_SYSTEM);

    /**
     * @brief Publish a configuration change event
     *
     * Convenience function for EVENT_CONFIG_CHANGED.
     *
     * @param config_path Path to changed config (e.g., "cvl_algorithm.enabled")
     * @param old_value Old value (string representation)
     * @param new_value New value (string representation)
     * @param source_id Source component (default: CONFIG_MANAGER)
     * @return true if published successfully
     */
    bool publishConfigChange(const char* config_path,
                             const char* old_value = "",
                             const char* new_value = "",
                             uint32_t source_id = SOURCE_ID_CONFIG_MANAGER);

    /**
     * @brief Publish a CVL state change event
     *
     * Convenience function for EVENT_CVL_STATE_CHANGED.
     *
     * @param old_state Previous CVL state
     * @param new_state New CVL state
     * @param new_cvl_voltage New CVL voltage (V)
     * @param new_ccl_current New CCL current (A)
     * @param new_dcl_current New DCL current (A)
     * @param state_duration_ms Time spent in old state (ms)
     * @param source_id Source component (default: CVL)
     * @return true if published successfully
     */
    bool publishCVLStateChange(uint8_t old_state, uint8_t new_state,
                               float new_cvl_voltage,
                               float new_ccl_current = 0.0,
                               float new_dcl_current = 0.0,
                              uint32_t state_duration_ms = 0,
                              uint32_t source_id = SOURCE_ID_CVL);

    /**
     * @brief Publish a human-readable status message
     *
     * Convenience function for EVENT_STATUS_MESSAGE.
     *
     * @param message Null-terminated status string (truncated to 63 chars)
     * @param source_id Source component (default: SYSTEM)
     * @param level Informational level (default: STATUS_LEVEL_INFO)
     * @return true if published successfully
     */
    bool publishStatus(const char* message,
                       uint32_t source_id = SOURCE_ID_SYSTEM,
                       StatusLevel level = STATUS_LEVEL_INFO);

    // ====================================================================================
    // SUBSCRIBING TO EVENTS
    // ====================================================================================

    /**
     * @brief Subscribe to an event type
     *
     * Registers a callback function to be called whenever an event of the
     * specified type is published.
     *
     * @param type Event type to subscribe to
     * @param callback Callback function to call
     * @param user_data Optional user data to pass to callback (default: nullptr)
     * @return true if subscription succeeded, false if max subscribers reached
     */
    bool subscribe(EventType type, EventCallback callback, void* user_data = nullptr);

    /**
     * @brief Subscribe to multiple event types
     *
     * Registers the same callback for multiple event types.
     *
     * @param types Array of event types
     * @param count Number of event types in array
     * @param callback Callback function to call
     * @param user_data Optional user data to pass to callback
     * @return true if all subscriptions succeeded
     */
    bool subscribeMultiple(const EventType* types, size_t count,
                          EventCallback callback, void* user_data = nullptr);

    /**
     * @brief Unsubscribe from an event type
     *
     * Removes a specific callback from an event type.
     *
     * @param type Event type to unsubscribe from
     * @param callback Callback function to remove
     * @return true if unsubscription succeeded (callback was found)
     */
    bool unsubscribe(EventType type, EventCallback callback);

    /**
     * @brief Unsubscribe from all event types
     *
     * Removes a callback from all event types it was subscribed to.
     *
     * @param callback Callback function to remove
     */
    void unsubscribeAll(EventCallback callback);

    // ====================================================================================
    // RETRIEVING LATEST EVENTS (CACHED)
    // ====================================================================================

    /**
     * @brief Get the latest event of a specific type
     *
     * This function retrieves the most recently published event of the specified
     * type from the cache. Similar to xQueuePeek() behavior.
     *
     * @param type Event type to retrieve
     * @param event_out Output: Latest event (filled if return is true)
     * @return true if a cached event exists, false otherwise
     */
    bool getLatest(EventType type, BusEvent& event_out);

    /**
     * @brief Get the latest live data
     *
     * Convenience function for getLatest(EVENT_LIVE_DATA_UPDATE).
     *
     * @param data_out Output: Latest TinyBMS live data
     * @return true if live data is available
     */
    bool getLatestLiveData(TinyBMS_LiveData& data_out);

    /**
     * @brief Check if a cached event exists for a type
     *
     * @param type Event type to check
     * @return true if the cache has an event of this type
     */
    bool hasLatest(EventType type);

    // ====================================================================================
    // STATISTICS AND MONITORING
    // ====================================================================================

    /**
     * @struct BusStats
     * @brief Statistics about Event Bus operation
     */
    struct BusStats {
        uint32_t total_events_published;    // Total events published since boot
        uint32_t total_events_dispatched;   // Total events dispatched to subscribers
        uint32_t queue_overruns;            // Events dropped due to full queue
        uint32_t dispatch_errors;           // Errors during event dispatch
        uint32_t total_subscribers;         // Current number of subscribers
        uint32_t current_queue_depth;       // Current number of events in queue
    };

    /**
     * @brief Get Event Bus statistics
     *
     * @param stats_out Output: Statistics structure (filled)
     */
    void getStats(BusStats& stats_out);

    /**
     * @brief Reset statistics counters
     */
    void resetStats();

    /**
     * @brief Get the number of subscribers for a specific event type
     *
     * @param type Event type
     * @return Number of subscribers
     */
    size_t getSubscriberCount(EventType type);

    // ====================================================================================
    // DEBUG AND DIAGNOSTICS
    // ====================================================================================

    /**
     * @brief Dump all subscribers to Serial (for debugging)
     *
     * Prints a list of all subscriptions grouped by event type.
     */
    void dumpSubscribers();

    /**
     * @brief Dump all cached events to Serial (for debugging)
     *
     * Prints the latest event for each event type (if cached).
     */
    void dumpLatestEvents();

    /**
     * @brief Get Event Bus statistics as JSON string
     *
     * @return JSON string with statistics
     */
    String getStatsJSON();

    /**
     * @brief Get subscriber list as JSON string
     *
     * @return JSON string with subscriber information
     */
    String getSubscribersJSON();

private:
    // ====================================================================================
    // PRIVATE CONSTRUCTOR (SINGLETON)
    // ====================================================================================

    EventBus();  // Private constructor
    ~EventBus(); // Private destructor

    // Disable copy and assignment
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    // ====================================================================================
    // INTERNAL DATA STRUCTURES
    // ====================================================================================

    /**
     * @struct Subscription
     * @brief Internal structure for subscriber registration
     */
    struct Subscription {
        EventType type;           // Event type to subscribe to
        EventCallback callback;   // Callback function
        void* user_data;          // User data to pass to callback
        uint32_t call_count;      // Number of times callback was called (stats)
    };

    // ====================================================================================
    // INTERNAL STATE
    // ====================================================================================

    bool initialized_;                              // Initialization flag
    QueueHandle_t event_queue_;                     // FreeRTOS queue for events
    SemaphoreHandle_t bus_mutex_;                   // Mutex for thread-safe access
    TaskHandle_t dispatch_task_handle_;             // Handle to dispatch task
    std::vector<Subscription> subscribers_;         // Subscriber registry
    BusEvent latest_events_[EVENT_TYPE_COUNT];      // Cache of latest events
    bool latest_events_valid_[EVENT_TYPE_COUNT];    // Validity flags for cache
    BusStats stats_;                                // Statistics
    uint32_t sequence_counter_;                     // Sequence number counter

    // ====================================================================================
    // INTERNAL FUNCTIONS
    // ====================================================================================

    /**
     * @brief Dispatch task main loop (static wrapper)
     */
    static void dispatchTaskWrapper(void* param);

    /**
     * @brief Dispatch task main loop (instance method)
     */
    void dispatchTask();

    /**
     * @brief Process a single event (call all subscribers)
     */
    void processEvent(const BusEvent& event);

    /**
     * @brief Update the event cache
     */
    void updateCache(const BusEvent& event);

    /**
     * @brief Validate event data size (if enabled)
     */
    bool validateDataSize(EventType type, size_t data_size);

    /**
     * @brief Log event publication (if enabled)
     */
    void logPublication(const BusEvent& event);

    /**
     * @brief Log event dispatch (if enabled)
     */
    void logDispatch(const BusEvent& event, const Subscription& sub);
};

// ====================================================================================
// GLOBAL INSTANCE
// ====================================================================================

/**
 * @brief Global Event Bus instance
 *
 * Usage:
 * @code
 * eventBus.begin();
 * eventBus.subscribe(EVENT_LIVE_DATA_UPDATE, myCallback);
 * eventBus.publishLiveData(data, SOURCE_ID_UART);
 * @endcode
 */
extern EventBus& eventBus;

#endif // EVENT_BUS_H
