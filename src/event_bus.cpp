/**
 * @file event_bus.cpp
 * @brief Implementation of the Event Bus singleton class
 */

#include "event_bus.h"

// Optional logging support
#ifdef LOGGER_AVAILABLE
#include "logger.h"
extern Logger logger;
#define EVENTBUS_LOG(level, msg) \
    do { \
        if (EVENT_BUS_DEBUG_ENABLED && logger.getLevel() >= level) { \
            logger.log(level, String("[EventBus] ") + msg); \
        } \
    } while(0)
#else
#define EVENTBUS_LOG(level, msg) do {} while(0)
#endif

// ====================================================================================
// SINGLETON INSTANCE
// ====================================================================================

EventBus& EventBus::getInstance() {
    static EventBus instance;
    return instance;
}

// Global convenience reference
EventBus& eventBus = EventBus::getInstance();

// ====================================================================================
// CONSTRUCTOR / DESTRUCTOR
// ====================================================================================

EventBus::EventBus()
    : initialized_(false)
    , event_queue_(nullptr)
    , bus_mutex_(nullptr)
    , dispatch_task_handle_(nullptr)
    , sequence_counter_(0)
{
    // Initialize stats
    memset(&stats_, 0, sizeof(stats_));

    // Initialize cache validity flags
    memset(latest_events_valid_, 0, sizeof(latest_events_valid_));

    // Reserve space for subscribers vector
    subscribers_.reserve(EVENT_BUS_MAX_SUBSCRIBERS);
}

EventBus::~EventBus() {
    // Clean up FreeRTOS resources
    if (dispatch_task_handle_) {
        vTaskDelete(dispatch_task_handle_);
        dispatch_task_handle_ = nullptr;
    }

    if (event_queue_) {
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
    }

    if (bus_mutex_) {
        vSemaphoreDelete(bus_mutex_);
        bus_mutex_ = nullptr;
    }
}

// ====================================================================================
// INITIALIZATION
// ====================================================================================

bool EventBus::begin(size_t queue_size) {
    if (initialized_) {
        EVENTBUS_LOG(LOG_WARNING, "Already initialized");
        return true;
    }

    // Create FreeRTOS queue
    event_queue_ = xQueueCreate(queue_size, sizeof(BusEvent));
    if (!event_queue_) {
        EVENTBUS_LOG(LOG_ERROR, "Failed to create event queue");
        return false;
    }

    // Create mutex for thread-safe access
    bus_mutex_ = xSemaphoreCreateMutex();
    if (!bus_mutex_) {
        EVENTBUS_LOG(LOG_ERROR, "Failed to create mutex");
        vQueueDelete(event_queue_);
        event_queue_ = nullptr;
        return false;
    }

    // Create dispatch task
    BaseType_t task_result = xTaskCreate(
        dispatchTaskWrapper,
        "eventBusDispatch",
        EVENT_BUS_TASK_STACK_SIZE,
        this,
        EVENT_BUS_TASK_PRIORITY,
        &dispatch_task_handle_
    );

    if (task_result != pdPASS) {
        EVENTBUS_LOG(LOG_ERROR, "Failed to create dispatch task");
        vSemaphoreDelete(bus_mutex_);
        vQueueDelete(event_queue_);
        bus_mutex_ = nullptr;
        event_queue_ = nullptr;
        return false;
    }

    initialized_ = true;
    EVENTBUS_LOG(LOG_INFO, "Initialized successfully (queue size: " + String(queue_size) + ")");

    return true;
}

// ====================================================================================
// PUBLISHING EVENTS
// ====================================================================================

bool EventBus::publish(EventType type, const void* data, size_t data_size,
                       uint32_t source_id, bool from_isr) {
    if (!initialized_) {
        return false;
    }

    // Validate data size if enabled
    #if EVENT_BUS_VALIDATE_DATA_SIZE
    if (!validateDataSize(type, data_size)) {
        EVENTBUS_LOG(LOG_ERROR, "Invalid data size for event type " + String(type));
        return false;
    }
    #endif

    // Build event
    BusEvent event;
    event.type = type;
    event.timestamp_ms = millis();
    event.source_id = source_id;
    event.sequence_number = sequence_counter_++;
    event.data_size = data_size;

    // Copy data into event (union)
    if (data && data_size > 0) {
        if (data_size > sizeof(event.data.raw_data)) {
            EVENTBUS_LOG(LOG_ERROR, "Data size too large: " + String(data_size));
            return false;
        }
        memcpy(&event.data, data, data_size);
    }

    // Publish to queue
    bool success;
    if (from_isr) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        success = (xQueueSendFromISR(event_queue_, &event, &higher_priority_task_woken) == pdTRUE);
        if (higher_priority_task_woken) {
            portYIELD_FROM_ISR();
        }
    } else {
        success = (xQueueSend(event_queue_, &event, 0) == pdTRUE);
    }

    // Update statistics
    if (success) {
        #if EVENT_BUS_STATS_ENABLED
        stats_.total_events_published++;
        #endif

        #if EVENT_BUS_LOG_PUBLICATIONS
        logPublication(event);
        #endif

        // Update cache
        #if EVENT_BUS_CACHE_ENABLED
        updateCache(event);
        #endif
    } else {
        #if EVENT_BUS_STATS_ENABLED
        stats_.queue_overruns++;
        #endif
        EVENTBUS_LOG(LOG_WARNING, "Queue full, event dropped (type=" + String(type) + ")");
    }

    return success;
}

bool EventBus::publishLiveData(const TinyBMS_LiveData& data, uint32_t source_id) {
    return publish(EVENT_LIVE_DATA_UPDATE, &data, sizeof(data), source_id);
}

bool EventBus::publishMqttRegister(const MqttRegisterEvent& data,
                                   uint32_t source_id,
                                   bool from_isr) {
    return publish(EVENT_MQTT_REGISTER_VALUE, &data, sizeof(data), source_id, from_isr);
}

bool EventBus::publishAlarm(uint16_t alarm_code, const char* message,
                            AlarmSeverity severity, float value,
                            uint32_t source_id) {
    AlarmEvent alarm;
    alarm.alarm_code = alarm_code;
    alarm.severity = severity;
    alarm.value = value;
    alarm.is_active = true;

    // Copy message safely
    strncpy(alarm.message, message, sizeof(alarm.message) - 1);
    alarm.message[sizeof(alarm.message) - 1] = '\0';

    return publish(EVENT_ALARM_RAISED, &alarm, sizeof(alarm), source_id);
}

bool EventBus::publishConfigChange(const char* config_path,
                                   const char* old_value,
                                   const char* new_value,
                                   uint32_t source_id) {
    ConfigChangeEvent config_change;

    // Copy strings safely
    strncpy(config_change.config_path, config_path, sizeof(config_change.config_path) - 1);
    config_change.config_path[sizeof(config_change.config_path) - 1] = '\0';

    strncpy(config_change.old_value, old_value, sizeof(config_change.old_value) - 1);
    config_change.old_value[sizeof(config_change.old_value) - 1] = '\0';

    strncpy(config_change.new_value, new_value, sizeof(config_change.new_value) - 1);
    config_change.new_value[sizeof(config_change.new_value) - 1] = '\0';

    return publish(EVENT_CONFIG_CHANGED, &config_change, sizeof(config_change), source_id);
}

bool EventBus::publishCVLStateChange(uint8_t old_state, uint8_t new_state,
                                     float new_cvl_voltage,
                                     float new_ccl_current,
                                     float new_dcl_current,
                                     uint32_t state_duration_ms,
                                     uint32_t source_id) {
    CVL_StateChange cvl_state;
    cvl_state.old_state = old_state;
    cvl_state.new_state = new_state;
    cvl_state.new_cvl_voltage = new_cvl_voltage;
    cvl_state.new_ccl_current = new_ccl_current;
    cvl_state.new_dcl_current = new_dcl_current;
    cvl_state.state_duration_ms = state_duration_ms;

    return publish(EVENT_CVL_STATE_CHANGED, &cvl_state, sizeof(cvl_state), source_id);
}

bool EventBus::publishStatus(const char* message, uint32_t source_id, StatusLevel level) {
    StatusEvent status_event{};
    status_event.level = static_cast<uint8_t>(level);

    if (message) {
        strncpy(status_event.message, message, sizeof(status_event.message) - 1);
        status_event.message[sizeof(status_event.message) - 1] = '\0';
    } else {
        status_event.message[0] = '\0';
    }

    return publish(EVENT_STATUS_MESSAGE, &status_event, sizeof(status_event), source_id);
}

// ====================================================================================
// SUBSCRIBING TO EVENTS
// ====================================================================================

bool EventBus::subscribe(EventType type, EventCallback callback, void* user_data) {
    if (!initialized_ || !callback) {
        return false;
    }

    // Check if we've reached max subscribers
    if (subscribers_.size() >= EVENT_BUS_MAX_SUBSCRIBERS) {
        EVENTBUS_LOG(LOG_ERROR, "Max subscribers reached");
        return false;
    }

    // Check max subscribers per type
    size_t count_for_type = 0;
    for (const auto& sub : subscribers_) {
        if (sub.type == type) {
            count_for_type++;
            if (count_for_type >= EVENT_BUS_MAX_SUBSCRIBERS_PER_TYPE) {
                EVENTBUS_LOG(LOG_ERROR, "Max subscribers per type reached for type " + String(type));
                return false;
            }
        }
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        EVENTBUS_LOG(LOG_ERROR, "Failed to acquire mutex for subscribe");
        return false;
    }

    // Add subscription
    Subscription sub;
    sub.type = type;
    sub.callback = callback;
    sub.user_data = user_data;
    sub.call_count = 0;

    subscribers_.push_back(sub);

    #if EVENT_BUS_STATS_ENABLED
    stats_.total_subscribers = subscribers_.size();
    #endif

    EVENTBUS_LOG(LOG_INFO, "Subscribed to event type " + String(type) +
                 " (total subscribers: " + String(subscribers_.size()) + ")");

    xSemaphoreGive(bus_mutex_);
    return true;
}

bool EventBus::subscribeMultiple(const EventType* types, size_t count,
                                 EventCallback callback, void* user_data) {
    if (!types || count == 0) {
        return false;
    }

    bool all_success = true;
    for (size_t i = 0; i < count; i++) {
        if (!subscribe(types[i], callback, user_data)) {
            all_success = false;
        }
    }

    return all_success;
}

bool EventBus::unsubscribe(EventType type, EventCallback callback) {
    if (!initialized_ || !callback) {
        return false;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    // Find and remove subscription
    bool found = false;
    for (auto it = subscribers_.begin(); it != subscribers_.end(); ) {
        if (it->type == type && it->callback == callback) {
            it = subscribers_.erase(it);
            found = true;
            EVENTBUS_LOG(LOG_INFO, "Unsubscribed from event type " + String(type));
            break;
        } else {
            ++it;
        }
    }

    #if EVENT_BUS_STATS_ENABLED
    stats_.total_subscribers = subscribers_.size();
    #endif

    xSemaphoreGive(bus_mutex_);
    return found;
}

void EventBus::unsubscribeAll(EventCallback callback) {
    if (!initialized_ || !callback) {
        return;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return;
    }

    // Remove all subscriptions for this callback
    size_t removed_count = 0;
    for (auto it = subscribers_.begin(); it != subscribers_.end(); ) {
        if (it->callback == callback) {
            it = subscribers_.erase(it);
            removed_count++;
        } else {
            ++it;
        }
    }

    #if EVENT_BUS_STATS_ENABLED
    stats_.total_subscribers = subscribers_.size();
    #endif

    EVENTBUS_LOG(LOG_INFO, "Unsubscribed from all events (removed " + String(removed_count) + " subscriptions)");

    xSemaphoreGive(bus_mutex_);
}

// ====================================================================================
// RETRIEVING LATEST EVENTS
// ====================================================================================

bool EventBus::getLatest(EventType type, BusEvent& event_out) {
    #if EVENT_BUS_CACHE_ENABLED
    if (!initialized_ || type >= EVENT_TYPE_COUNT) {
        return false;
    }

    if (!latest_events_valid_[type]) {
        return false;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return false;
    }

    event_out = latest_events_[type];

    xSemaphoreGive(bus_mutex_);
    return true;
    #else
    return false; // Cache disabled
    #endif
}

bool EventBus::getLatestLiveData(TinyBMS_LiveData& data_out) {
    BusEvent event;
    if (getLatest(EVENT_LIVE_DATA_UPDATE, event)) {
        data_out = event.data.live_data;
        return true;
    }
    return false;
}

bool EventBus::hasLatest(EventType type) {
    #if EVENT_BUS_CACHE_ENABLED
    if (type >= EVENT_TYPE_COUNT) {
        return false;
    }
    return latest_events_valid_[type];
    #else
    return false;
    #endif
}

// ====================================================================================
// STATISTICS AND MONITORING
// ====================================================================================

void EventBus::getStats(BusStats& stats_out) {
    #if EVENT_BUS_STATS_ENABLED
    if (!initialized_) {
        memset(&stats_out, 0, sizeof(stats_out));
        return;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        memset(&stats_out, 0, sizeof(stats_out));
        return;
    }

    stats_out = stats_;
    stats_out.current_queue_depth = uxQueueMessagesWaiting(event_queue_);

    xSemaphoreGive(bus_mutex_);
    #else
    memset(&stats_out, 0, sizeof(stats_out));
    #endif
}

void EventBus::resetStats() {
    #if EVENT_BUS_STATS_ENABLED
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        stats_.total_events_published = 0;
        stats_.total_events_dispatched = 0;
        stats_.queue_overruns = 0;
        stats_.dispatch_errors = 0;
        // Don't reset total_subscribers and current_queue_depth

        xSemaphoreGive(bus_mutex_);
    }
    #endif
}

size_t EventBus::getSubscriberCount(EventType type) {
    if (!initialized_) {
        return 0;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return 0;
    }

    size_t count = 0;
    for (const auto& sub : subscribers_) {
        if (sub.type == type) {
            count++;
        }
    }

    xSemaphoreGive(bus_mutex_);
    return count;
}

// ====================================================================================
// DEBUG AND DIAGNOSTICS
// ====================================================================================

void EventBus::dumpSubscribers() {
    Serial.println("\n=== Event Bus Subscribers ===");

    if (!initialized_) {
        Serial.println("Event Bus not initialized");
        return;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        Serial.println("Failed to acquire mutex");
        return;
    }

    Serial.printf("Total subscribers: %d\n\n", subscribers_.size());

    // Group by event type
    for (int type = 0; type < EVENT_TYPE_COUNT; type++) {
        size_t count = 0;
        for (const auto& sub : subscribers_) {
            if (sub.type == (EventType)type) {
                count++;
            }
        }

        if (count > 0) {
            Serial.printf("Event Type %d (%s): %d subscribers\n",
                         type, BusEvent::getEventTypeName((EventType)type), count);

            #if EVENT_BUS_PER_SUBSCRIBER_STATS
            for (const auto& sub : subscribers_) {
                if (sub.type == (EventType)type) {
                    Serial.printf("  - Callback: 0x%08X, Call count: %u\n",
                                 (uint32_t)sub.callback, sub.call_count);
                }
            }
            #endif
        }
    }

    xSemaphoreGive(bus_mutex_);
    Serial.println("=============================\n");
}

void EventBus::dumpLatestEvents() {
    #if EVENT_BUS_CACHE_ENABLED
    Serial.println("\n=== Latest Events Cache ===");

    if (!initialized_) {
        Serial.println("Event Bus not initialized");
        return;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        Serial.println("Failed to acquire mutex");
        return;
    }

    for (int type = 0; type < EVENT_TYPE_COUNT; type++) {
        if (latest_events_valid_[type]) {
            const BusEvent& event = latest_events_[type];
            Serial.printf("Event Type %d (%s): Seq=%u, Time=%u ms ago, Source=%u\n",
                         type,
                         BusEvent::getEventTypeName((EventType)type),
                         event.sequence_number,
                         millis() - event.timestamp_ms,
                         event.source_id);
        }
    }

    xSemaphoreGive(bus_mutex_);
    Serial.println("===========================\n");
    #else
    Serial.println("Event cache disabled");
    #endif
}

String EventBus::getStatsJSON() {
    BusStats stats;
    getStats(stats);

    String json = "{";
    json += "\"total_events_published\":" + String(stats.total_events_published) + ",";
    json += "\"total_events_dispatched\":" + String(stats.total_events_dispatched) + ",";
    json += "\"queue_overruns\":" + String(stats.queue_overruns) + ",";
    json += "\"dispatch_errors\":" + String(stats.dispatch_errors) + ",";
    json += "\"total_subscribers\":" + String(stats.total_subscribers) + ",";
    json += "\"current_queue_depth\":" + String(stats.current_queue_depth);
    json += "}";

    return json;
}

String EventBus::getSubscribersJSON() {
    if (!initialized_) {
        return "{\"error\":\"not_initialized\"}";
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return "{\"error\":\"mutex_timeout\"}";
    }

    String json = "{\"subscribers\":[";

    bool first = true;
    for (const auto& sub : subscribers_) {
        if (!first) json += ",";
        first = false;

        json += "{";
        json += "\"type\":" + String((int)sub.type) + ",";
        json += "\"type_name\":\"" + String(BusEvent::getEventTypeName(sub.type)) + "\",";
        json += "\"callback\":\"0x" + String((uint32_t)sub.callback, HEX) + "\"";
        #if EVENT_BUS_PER_SUBSCRIBER_STATS
        json += ",\"call_count\":" + String(sub.call_count);
        #endif
        json += "}";
    }

    json += "],\"total\":" + String(subscribers_.size()) + "}";

    xSemaphoreGive(bus_mutex_);
    return json;
}

// ====================================================================================
// DISPATCH TASK
// ====================================================================================

void EventBus::dispatchTaskWrapper(void* param) {
    EventBus* instance = static_cast<EventBus*>(param);
    instance->dispatchTask();
}

void EventBus::dispatchTask() {
    BusEvent event;

    EVENTBUS_LOG(LOG_INFO, "Dispatch task started");

    while (true) {
        // Wait for event from queue
        if (xQueueReceive(event_queue_, &event, pdMS_TO_TICKS(EVENT_BUS_DISPATCH_INTERVAL_MS)) == pdTRUE) {
            // Process the event
            processEvent(event);
        }

        // Yield to other tasks
        taskYIELD();
    }
}

void EventBus::processEvent(const BusEvent& event) {
    #if EVENT_BUS_LOG_DISPATCHES
    EVENTBUS_LOG(LOG_DEBUG, "Dispatching event type " + String(event.type) +
                 " seq=" + String(event.sequence_number));
    #endif

    // Acquire mutex to read subscriber list
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        #if EVENT_BUS_STATS_ENABLED
        stats_.dispatch_errors++;
        #endif
        return;
    }

    // Find all subscribers for this event type
    std::vector<Subscription*> matching_subs;
    for (auto& sub : subscribers_) {
        if (sub.type == event.type) {
            matching_subs.push_back(&sub);
        }
    }

    xSemaphoreGive(bus_mutex_);

    // Call all matching callbacks (outside of mutex to prevent deadlock)
    for (auto* sub : matching_subs) {
        #if EVENT_BUS_CHECK_NULL_CALLBACKS
        if (!sub->callback) {
            continue;
        }
        #endif

        // Measure callback execution time (if enabled)
        #if EVENT_BUS_MAX_CALLBACK_TIME_MS > 0
        uint32_t start_time = millis();
        #endif

        // Call the callback
        sub->callback(event, sub->user_data);

        // Check callback execution time
        #if EVENT_BUS_MAX_CALLBACK_TIME_MS > 0
        uint32_t elapsed = millis() - start_time;
        if (elapsed > EVENT_BUS_MAX_CALLBACK_TIME_MS) {
            EVENTBUS_LOG(LOG_WARNING, "Slow callback detected: " + String(elapsed) +
                         " ms for event type " + String(event.type));
        }
        #endif

        // Update statistics
        #if EVENT_BUS_STATS_ENABLED
        stats_.total_events_dispatched++;
        #endif

        #if EVENT_BUS_PER_SUBSCRIBER_STATS
        // Acquire mutex to update call count
        if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) == pdTRUE) {
            sub->call_count++;
            xSemaphoreGive(bus_mutex_);
        }
        #endif

        #if EVENT_BUS_LOG_DISPATCHES
        logDispatch(event, *sub);
        #endif
    }
}

// ====================================================================================
// INTERNAL HELPER FUNCTIONS
// ====================================================================================

void EventBus::updateCache(const BusEvent& event) {
    #if EVENT_BUS_CACHE_ENABLED
    if (event.type >= EVENT_TYPE_COUNT) {
        return;
    }

    // Acquire mutex
    if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(EVENT_BUS_MUTEX_TIMEOUT_MS)) == pdTRUE) {
        latest_events_[event.type] = event;
        latest_events_valid_[event.type] = true;
        xSemaphoreGive(bus_mutex_);
    }
    #endif
}

bool EventBus::validateDataSize(EventType type, size_t data_size) {
    // Validate that data_size is reasonable
    if (data_size > sizeof(((BusEvent*)0)->data.raw_data)) {
        return false;
    }

    // Type-specific validation (optional, can be expanded)
    switch (type) {
        case EVENT_LIVE_DATA_UPDATE:
            return data_size == sizeof(TinyBMS_LiveData);
        case EVENT_CVL_STATE_CHANGED:
            return data_size == sizeof(CVL_StateChange);
        case EVENT_ALARM_RAISED:
        case EVENT_ALARM_CLEARED:
        case EVENT_WARNING_RAISED:
            return data_size == sizeof(AlarmEvent);
        case EVENT_CONFIG_CHANGED:
            return data_size == sizeof(ConfigChangeEvent);
        case EVENT_COMMAND_RECEIVED:
        case EVENT_COMMAND_RESPONSE:
            return data_size == sizeof(CommandEvent);
        case EVENT_STATUS_MESSAGE:
            return data_size == sizeof(StatusEvent);
        default:
            // For other types, just check max size
            return data_size <= sizeof(((BusEvent*)0)->data.raw_data);
    }
}

void EventBus::logPublication(const BusEvent& event) {
    #ifdef LOGGER_AVAILABLE
    EVENTBUS_LOG(LOG_DEBUG, "Published: " + event.toString());
    #endif
}

void EventBus::logDispatch(const BusEvent& event, const Subscription& sub) {
    #ifdef LOGGER_AVAILABLE
    String msg = "Dispatched event " + String(event.type) + " to callback 0x";
    msg += String((uint32_t)sub.callback, HEX);
    EVENTBUS_LOG(LOG_DEBUG, msg);
    #endif
}
