/**
 * @file bridge_core.cpp
 * @brief Core init & task creation
 */
#include <Arduino.h>
#include <algorithm>
#include "bridge_core.h"
#include "bridge_uart.h"
#include "bridge_can.h"
#include "bridge_cvl.h"
#include "bridge_keepalive.h"
#include "logger.h"
#include "config_manager.h"
#include "watchdog_manager.h"
#include "rtos_tasks.h"
#include "rtos_config.h"
#include "hal/hal_manager.h"
#include "hal/interfaces/ihal_can.h"
#include "hal/interfaces/ihal_uart.h"

extern Logger logger;
extern ConfigManager config;
extern SemaphoreHandle_t uartMutex;
extern SemaphoreHandle_t feedMutex;
extern SemaphoreHandle_t configMutex;
extern WatchdogManager Watchdog;

#define BRIDGE_LOG(level, msg) do { logger.log(level, String("[BRIDGE] ") + (msg)); } while(0)

TinyBMS_Victron_Bridge::TinyBMS_Victron_Bridge()
    : tiny_uart_(nullptr),
      uart_poller_(),
      uart_rx_buffer_(256),
      event_sink_(nullptr),
      initialized_(false),
      victron_keepalive_ok_(false) {
    memset(&config_, 0, sizeof(config_));
    stats = BridgeStats{};
    last_uart_poll_ms_ = last_pgn_update_ms_ = last_cvl_update_ms_ = 0;
    last_keepalive_tx_ms_ = last_keepalive_rx_ms_ = 0;
    uart_poll_interval_ms_ = UART_POLL_INTERVAL_MS;
    pgn_update_interval_ms_ = PGN_UPDATE_INTERVAL_MS;
    cvl_update_interval_ms_ = CVL_UPDATE_INTERVAL_MS;
    keepalive_interval_ms_ = 1000;
    keepalive_timeout_ms_ = 10000;
}

bool TinyBMS_Victron_Bridge::begin() {
    BRIDGE_LOG(LOG_INFO, "Initializing TinyBMS-Victron Bridge...");

    if (event_sink_ == nullptr) {
        BRIDGE_LOG(LOG_ERROR, "Event sink not configured");
        return false;
    }

    hal::HalManager& hal_manager = hal::HalManager::instance();
    if (!hal_manager.isInitialized()) {
        BRIDGE_LOG(LOG_ERROR, "HAL manager not initialized");
        return false;
    }

    ConfigManager::HardwareConfig::UART uart_cfg{};
    ConfigManager::HardwareConfig::CAN can_cfg{};
    ConfigManager::TinyBMSConfig tinybms_cfg{};
    ConfigManager::VictronConfig victron_cfg{};

    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        uart_cfg = config.hardware.uart;
        can_cfg = config.hardware.can;
        tinybms_cfg = config.tinybms;
        victron_cfg = config.victron;
        xSemaphoreGive(configMutex);
    } else {
        BRIDGE_LOG(LOG_WARN, "Using default configuration values (config mutex unavailable)");
    }

    hal::UartConfig hal_uart_config{};
    hal_uart_config.rx_pin = uart_cfg.rx_pin;
    hal_uart_config.tx_pin = uart_cfg.tx_pin;
    hal_uart_config.baudrate = uart_cfg.baudrate;
    hal_uart_config.timeout_ms = uart_cfg.timeout_ms;
    hal_uart_config.use_dma = true;

    hal::CanConfig hal_can_config{};
    hal_can_config.tx_pin = can_cfg.tx_pin;
    hal_can_config.rx_pin = can_cfg.rx_pin;
    hal_can_config.bitrate = can_cfg.bitrate;
    hal_can_config.enable_termination = can_cfg.termination;
    hal_can_config.filters.clear();
    hal::CanFilterConfig keepalive_filter{};
    keepalive_filter.id = VICTRON_PGN_KEEPALIVE;
    keepalive_filter.mask = 0x7FF;
    keepalive_filter.extended = false;
    hal_can_config.filters.push_back(keepalive_filter);

    tiny_uart_ = &hal_manager.uart();

    if (xSemaphoreTake(uartMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        if (tiny_uart_->initialize(hal_uart_config) != hal::Status::Ok) {
            xSemaphoreGive(uartMutex);
            BRIDGE_LOG(LOG_ERROR, "UART HAL initialization failed");
            return false;
        }
        tiny_uart_->setTimeout(hal_uart_config.timeout_ms);
        xSemaphoreGive(uartMutex);
        BRIDGE_LOG(LOG_INFO, "UART initialized via HAL");
    } else {
        BRIDGE_LOG(LOG_ERROR, "UART mutex not available during init");
        return false;
    }

    BRIDGE_LOG(LOG_INFO, "Initializing CAN via HAL...");
    hal::IHalCan& can_hal = hal_manager.can();
    if (can_hal.initialize(hal_can_config) != hal::Status::Ok) {
        BRIDGE_LOG(LOG_ERROR, "CAN HAL init failed");
        return false;
    }
    BRIDGE_LOG(LOG_INFO, "CAN initialized OK");

    optimization::AdaptivePollingConfig poll_cfg{};
    poll_cfg.base_interval_ms = std::max<uint32_t>(20, tinybms_cfg.poll_interval_ms);
    poll_cfg.min_interval_ms = std::max<uint32_t>(20, tinybms_cfg.poll_interval_min_ms);
    poll_cfg.max_interval_ms = std::max<uint32_t>(poll_cfg.min_interval_ms, tinybms_cfg.poll_interval_max_ms);
    poll_cfg.backoff_step_ms = std::max<uint32_t>(1, tinybms_cfg.poll_backoff_step_ms);
    poll_cfg.recovery_step_ms = std::max<uint32_t>(1, tinybms_cfg.poll_recovery_step_ms);
    poll_cfg.latency_target_ms = std::max<uint32_t>(5, tinybms_cfg.poll_latency_target_ms);
    poll_cfg.latency_slack_ms = tinybms_cfg.poll_latency_slack_ms;
    poll_cfg.failure_threshold = std::max<uint8_t>(static_cast<uint8_t>(1), tinybms_cfg.poll_failure_threshold);
    poll_cfg.success_threshold = std::max<uint8_t>(static_cast<uint8_t>(1), tinybms_cfg.poll_success_threshold);
    uart_poller_.configure(poll_cfg);
    uart_poll_interval_ms_  = uart_poller_.currentInterval();
    pgn_update_interval_ms_ = std::max<uint32_t>(100, victron_cfg.pgn_update_interval_ms);
    cvl_update_interval_ms_ = std::max<uint32_t>(500, victron_cfg.cvl_update_interval_ms);
    keepalive_interval_ms_  = std::max<uint32_t>(200, victron_cfg.keepalive_interval_ms);
    keepalive_timeout_ms_   = std::max<uint32_t>(1000, victron_cfg.keepalive_timeout_ms);

    last_keepalive_rx_ms_ = millis();
    stats.victron_keepalive_ok = false;
    victron_keepalive_ok_ = false;

    BRIDGE_LOG(LOG_INFO, String("Intervals: UART=") + uart_poll_interval_ms_ +
                             "ms (min=" + poll_cfg.min_interval_ms +
                             "ms max=" + poll_cfg.max_interval_ms +
                             "ms target=" + poll_cfg.latency_target_ms +
                             "ms), PGN=" + pgn_update_interval_ms_ +
                             "ms, CVL=" + cvl_update_interval_ms_ +
                             "ms, KA tx=" + keepalive_interval_ms_ +
                             "ms, KA timeout=" + keepalive_timeout_ms_ + "ms");

    stats.uart_poll_interval_current_ms = uart_poll_interval_ms_;
    stats.uart_latency_avg_ms = 0.0f;
    stats.uart_latency_last_ms = 0;
    stats.uart_latency_max_ms = 0;
    stats.websocket_sent_count = 0;
    stats.websocket_dropped_count = 0;

    initialized_ = true;
    BRIDGE_LOG(LOG_INFO, "Bridge init complete");
    return true;
}

void TinyBMS_Victron_Bridge::setMqttPublisher(mqtt::Publisher* publisher) {
    mqtt_publisher_ = publisher;
}

void TinyBMS_Victron_Bridge::setEventSink(BridgeEventSink* sink) {
    event_sink_ = sink;
}

void TinyBMS_Victron_Bridge::setUart(hal::IHalUart* uart) {
    tiny_uart_ = uart;
    uart_rx_buffer_.clear();
}

BridgeEventSink& TinyBMS_Victron_Bridge::eventSink() const {
    return *event_sink_;
}

bool Bridge_BuildAndBegin(TinyBMS_Victron_Bridge& bridge, BridgeEventSink& sink) {
    bridge.setEventSink(&sink);
    return bridge.begin();
}

bool Bridge_CreateTasks(TinyBMS_Victron_Bridge* bridge) {
    if (!bridge || !bridge->initialized_) return false;

    const uint32_t uart_stack = TASK_DEFAULT_STACK_SIZE;
    const uint32_t can_stack  = TASK_DEFAULT_STACK_SIZE;
    const uint32_t cvl_stack  = TASK_DEFAULT_STACK_SIZE;

    BaseType_t ok1 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::uartTask, "UART_Task",
                      uart_stack, bridge, TASK_HIGH_PRIORITY, nullptr, 1);
    BaseType_t ok2 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::canTask, "CAN_Task",
                      can_stack, bridge, TASK_HIGH_PRIORITY, nullptr, 1);
    BaseType_t ok3 = xTaskCreatePinnedToCore(TinyBMS_Victron_Bridge::cvlTask, "CVL_Task",
                      cvl_stack, bridge, TASK_NORMAL_PRIORITY, nullptr, 1);

    return (ok1 == pdPASS && ok2 == pdPASS && ok3 == pdPASS);
}

TinyBMS_Config TinyBMS_Victron_Bridge::getConfig() const {
    return config_;
}
