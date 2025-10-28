/**
 * @file event_types.h
 * @brief Event types and data structures for the Event Bus architecture
 *
 * This file defines all event types, data structures, and payloads used
 * in the publish/subscribe Event Bus system.
 */

#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <Arduino.h>
#include "shared_data.h"

// ====================================================================================
// EVENT TYPE ENUMERATION
// ====================================================================================

/**
 * @enum EventType
 * @brief All supported event types in the system
 *
 * Events are categorized into:
 * - Real-time data events
 * - Configuration events
 * - Alarm and warning events
 * - Command and control events
 * - CVL algorithm events
 * - System status events
 * - Connectivity events
 */
enum EventType {
    // ==================== Real-time Data Events ====================
    EVENT_LIVE_DATA_UPDATE = 0,       // New BMS data from UART
    EVENT_CAN_DATA_RECEIVED = 1,      // Data received from CAN Bus

    // ==================== Configuration Events ====================
    EVENT_CONFIG_CHANGED = 10,        // Configuration modified
    EVENT_CONFIG_LOADED = 11,         // Configuration loaded at startup
    EVENT_CONFIG_SAVE_REQUEST = 12,   // Request to save configuration

    // ==================== Alarm and Status Events ====================
    EVENT_ALARM_RAISED = 20,          // Alarm triggered (voltage, temp, etc.)
    EVENT_ALARM_CLEARED = 21,         // Alarm cleared
    EVENT_WARNING_RAISED = 22,        // Warning (less severe than alarm)

    // ==================== Command Events ====================
    EVENT_COMMAND_RECEIVED = 30,      // Command received (Web, CAN, etc.)
    EVENT_COMMAND_RESPONSE = 31,      // Response to a command

    // ==================== CVL Algorithm Events ====================
    EVENT_CVL_STATE_CHANGED = 40,     // CVL state transition
    EVENT_CVL_LIMITS_UPDATED = 41,    // New CVL limits calculated

    // ==================== System Events ====================
    EVENT_SYSTEM_STATUS = 50,         // System status (uptime, health, etc.)
    EVENT_WATCHDOG_FED = 51,          // Watchdog fed
    EVENT_ERROR_OCCURRED = 52,        // System error
    EVENT_STATUS_MESSAGE = 53,        // Human-readable status message

    // ==================== Connectivity Events ====================
    EVENT_WIFI_CONNECTED = 60,        // WiFi connected
    EVENT_WIFI_DISCONNECTED = 61,     // WiFi disconnected
    EVENT_WEBSOCKET_CLIENT_CONNECTED = 62,    // WebSocket client connected
    EVENT_WEBSOCKET_CLIENT_DISCONNECTED = 63, // WebSocket client disconnected

    // ==================== Total Count ====================
    EVENT_TYPE_COUNT = 64             // Total number of event types
};

// ====================================================================================
// SOURCE IDs (for event.source_id)
// ====================================================================================

enum EventSource {
    SOURCE_ID_UNKNOWN = 0,
    SOURCE_ID_UART = 1,
    SOURCE_ID_CAN = 2,
    SOURCE_ID_WEBSOCKET = 3,
    SOURCE_ID_WEB_API = 4,
    SOURCE_ID_CVL = 5,
    SOURCE_ID_CONFIG_MANAGER = 6,
    SOURCE_ID_WATCHDOG = 7,
    SOURCE_ID_LOGGER = 8,
    SOURCE_ID_SYSTEM = 9
};

// ====================================================================================
// EVENT DATA STRUCTURES
// ====================================================================================

/**
 * @struct CVL_StateChange
 * @brief Data for EVENT_CVL_STATE_CHANGED
 */
struct CVL_StateChange {
    uint8_t old_state;              // Previous CVL state
    uint8_t new_state;              // New CVL state
    float new_cvl_voltage;          // New CVL voltage (V)
    float new_ccl_current;          // New CCL current (A)
    float new_dcl_current;          // New DCL current (A)
    uint32_t state_duration_ms;     // Time spent in old state (ms)
};

/**
 * @enum AlarmSeverity
 * @brief Alarm severity levels
 */
enum AlarmSeverity {
    ALARM_SEVERITY_INFO = 0,
    ALARM_SEVERITY_WARNING = 1,
    ALARM_SEVERITY_ERROR = 2,
    ALARM_SEVERITY_CRITICAL = 3
};

/**
 * @enum StatusLevel
 * @brief Informational level for status messages published on the bus
 */
enum StatusLevel : uint8_t {
    STATUS_LEVEL_INFO = 0,
    STATUS_LEVEL_NOTICE = 1,
    STATUS_LEVEL_WARNING = 2,
    STATUS_LEVEL_ERROR = 3
};

/**
 * @enum AlarmCode
 * @brief Standard alarm codes
 */
enum AlarmCode {
    ALARM_NONE = 0,

    // Voltage alarms
    ALARM_OVERVOLTAGE = 1,
    ALARM_UNDERVOLTAGE = 2,
    ALARM_CELL_OVERVOLTAGE = 3,
    ALARM_CELL_UNDERVOLTAGE = 4,

    // Current alarms
    ALARM_OVERCURRENT_CHARGE = 10,
    ALARM_OVERCURRENT_DISCHARGE = 11,

    // Temperature alarms
    ALARM_OVERTEMPERATURE = 20,
    ALARM_UNDERTEMPERATURE = 21,
    ALARM_LOW_T_CHARGE    = 22,

    // Cell imbalance alarms
    ALARM_CELL_IMBALANCE = 30,

    // Communication alarms
    ALARM_UART_ERROR = 40,
    ALARM_UART_TIMEOUT = 41,
    ALARM_CAN_ERROR = 42,
    ALARM_CAN_TIMEOUT = 43,
    ALARM_CAN_TX_ERROR = 44,
    ALARM_CAN_KEEPALIVE_LOST = 45,

    // System alarms
    ALARM_WATCHDOG_RESET = 50,
    ALARM_CONFIG_ERROR = 51,
    ALARM_MEMORY_ERROR = 52,

    // BMS Status alarms
    ALARM_BMS_OFFLINE = 60,
    ALARM_BMS_FAULT = 61
};

/**
 * @struct AlarmEvent
 * @brief Data for EVENT_ALARM_RAISED / EVENT_ALARM_CLEARED / EVENT_WARNING_RAISED
 */
struct AlarmEvent {
    uint16_t alarm_code;            // Alarm code (AlarmCode enum)
    uint8_t severity;               // Severity (AlarmSeverity enum)
    char message[64];               // Human-readable message
    float value;                    // Value that triggered the alarm (optional)
    bool is_active;                 // true = raised, false = cleared
};

/**
 * @struct ConfigChangeEvent
 * @brief Data for EVENT_CONFIG_CHANGED
 */
struct ConfigChangeEvent {
    char config_path[64];           // Path to changed config (e.g., "cvl_algorithm.enabled")
    char old_value[32];             // Old value (string representation)
    char new_value[32];             // New value (string representation)
};

/**
 * @enum CommandType
 * @brief Command types
 */
enum CommandType {
    CMD_REBOOT = 0,
    CMD_RESET_CONFIG = 1,
    CMD_ENABLE_WATCHDOG = 2,
    CMD_DISABLE_WATCHDOG = 3,
    CMD_FORCE_CVL_STATE = 4,
    CMD_CALIBRATE_SOC = 5,
    CMD_CLEAR_ALARMS = 6,
    CMD_CUSTOM = 99
};

/**
 * @struct CommandEvent
 * @brief Data for EVENT_COMMAND_RECEIVED / EVENT_COMMAND_RESPONSE
 */
struct CommandEvent {
    uint16_t command_type;          // CommandType enum
    uint32_t command_id;            // Unique command ID for tracking
    char payload[64];               // Command payload (JSON string or custom data)
    bool is_response;               // true = response, false = request
    bool success;                   // Response: command success status
    char error_message[32];         // Response: error message if failed
};

/**
 * @struct SystemStatusEvent
 * @brief Data for EVENT_SYSTEM_STATUS
 */
struct SystemStatusEvent {
    uint32_t uptime_ms;             // System uptime in milliseconds
    uint32_t free_heap_bytes;       // Free heap memory (bytes)
    uint8_t cpu_usage_percent;      // CPU usage (0-100%)
    uint8_t wifi_rssi_dbm;          // WiFi signal strength (dBm, 0 if not connected)
    bool watchdog_enabled;          // Watchdog status
    uint32_t total_events_published; // Total events published since boot
};

/**
 * @struct StatusEvent
 * @brief Data for EVENT_STATUS_MESSAGE
 */
struct StatusEvent {
    char message[64];               // Human-readable status message
    uint8_t level;                  // Informational level (StatusLevel enum)
};

/**
 * @struct WiFiEvent
 * @brief Data for EVENT_WIFI_CONNECTED / EVENT_WIFI_DISCONNECTED
 */
struct WiFiEvent {
    char ssid[32];                  // WiFi SSID
    int8_t rssi_dbm;                // Signal strength (dBm)
    uint8_t ip_address[4];          // IP address (IPv4)
    bool is_connected;              // Connection status
};

/**
 * @struct WebSocketClientEvent
 * @brief Data for EVENT_WEBSOCKET_CLIENT_CONNECTED / EVENT_WEBSOCKET_CLIENT_DISCONNECTED
 */
struct WebSocketClientEvent {
    uint32_t client_id;             // WebSocket client ID
    uint8_t ip_address[4];          // Client IP address
    bool is_connected;              // Connection status
};

// ====================================================================================
// MAIN EVENT STRUCTURE
// ====================================================================================

/**
 * @struct BusEvent
 * @brief Main event structure passed through the Event Bus
 *
 * This structure contains:
 * - Event metadata (type, timestamp, source, sequence)
 * - Event payload (union of all possible data types)
 *
 * The union ensures efficient memory usage by storing only one payload type at a time.
 */
struct BusEvent {
    // ==================== Metadata ====================
    EventType type;                 // Event type
    uint32_t timestamp_ms;          // Timestamp (ms since boot)
    uint32_t source_id;             // Source component ID (EventSource enum)
    uint32_t sequence_number;       // Sequence number (for debugging/tracking)

    // ==================== Payload (Union) ====================
    union {
        TinyBMS_LiveData live_data;           // For EVENT_LIVE_DATA_UPDATE
        CVL_StateChange cvl_state;            // For EVENT_CVL_STATE_CHANGED
        AlarmEvent alarm;                     // For EVENT_ALARM_*
        ConfigChangeEvent config_change;      // For EVENT_CONFIG_CHANGED
        CommandEvent command;                 // For EVENT_COMMAND_*
        SystemStatusEvent system_status;      // For EVENT_SYSTEM_STATUS
        StatusEvent status;                   // For EVENT_STATUS_MESSAGE
        WiFiEvent wifi;                       // For EVENT_WIFI_*
        WebSocketClientEvent websocket;       // For EVENT_WEBSOCKET_CLIENT_*
        uint8_t raw_data[128];                // Fallback for custom data
    } data;

    size_t data_size;               // Actual size of data (for validation)

    /**
     * @brief Returns a string representation of the event (for debugging)
     */
    String toString() const {
        String out;
        out.reserve(128);
        out += "[Event] Type=";
        out += getEventTypeName(type);
        out += ", Source=";
        out += source_id;
        out += ", Seq=";
        out += sequence_number;
        out += ", Time=";
        out += timestamp_ms;
        out += "ms";
        return out;
    }

    /**
     * @brief Returns the name of an event type
     */
    static const char* getEventTypeName(EventType type) {
        switch(type) {
            case EVENT_LIVE_DATA_UPDATE: return "LIVE_DATA_UPDATE";
            case EVENT_CAN_DATA_RECEIVED: return "CAN_DATA_RECEIVED";
            case EVENT_CONFIG_CHANGED: return "CONFIG_CHANGED";
            case EVENT_CONFIG_LOADED: return "CONFIG_LOADED";
            case EVENT_CONFIG_SAVE_REQUEST: return "CONFIG_SAVE_REQUEST";
            case EVENT_ALARM_RAISED: return "ALARM_RAISED";
            case EVENT_ALARM_CLEARED: return "ALARM_CLEARED";
            case EVENT_WARNING_RAISED: return "WARNING_RAISED";
            case EVENT_COMMAND_RECEIVED: return "COMMAND_RECEIVED";
            case EVENT_COMMAND_RESPONSE: return "COMMAND_RESPONSE";
            case EVENT_CVL_STATE_CHANGED: return "CVL_STATE_CHANGED";
            case EVENT_CVL_LIMITS_UPDATED: return "CVL_LIMITS_UPDATED";
            case EVENT_SYSTEM_STATUS: return "SYSTEM_STATUS";
            case EVENT_WATCHDOG_FED: return "WATCHDOG_FED";
            case EVENT_ERROR_OCCURRED: return "ERROR_OCCURRED";
            case EVENT_STATUS_MESSAGE: return "STATUS_MESSAGE";
            case EVENT_WIFI_CONNECTED: return "WIFI_CONNECTED";
            case EVENT_WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
            case EVENT_WEBSOCKET_CLIENT_CONNECTED: return "WEBSOCKET_CLIENT_CONNECTED";
            case EVENT_WEBSOCKET_CLIENT_DISCONNECTED: return "WEBSOCKET_CLIENT_DISCONNECTED";
            default: return "UNKNOWN";
        }
    }
};

#endif // EVENT_TYPES_H
