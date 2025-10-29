/**
 * @file logger.cpp
 * @brief Implémentation du système de journalisation pour TinyBMS-Victron Bridge
 * @version 1.0
 *
 * Gère les logs avec niveaux configurables, stockage sur SPIFFS, et rotation des fichiers.
 * Intégration avec FreeRTOS via log_mutex_ et configMutex.
 */

#include "logger.h"
#include "config_manager.h"
#include "hal/hal_manager.h"
#include "hal/interfaces/ihal_storage.h"

#include <vector>

extern SemaphoreHandle_t configMutex;
extern ConfigManager config;

Logger::Logger() : current_level_(LOG_INFO), initialized_(false) {
    log_mutex_ = xSemaphoreCreateMutex();
    if (log_mutex_ == NULL) {
        Serial.println("[LOGGER] ❌ Échec création mutex log");
    }
}

Logger::~Logger() {
    if (log_file_) {
        log_file_->close();
    }
    if (log_mutex_ != NULL) {
        vSemaphoreDelete(log_mutex_);
    }
}

bool Logger::begin(const ConfigManager& config) {
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        current_level_ = static_cast<LogLevel>(config.logging.log_level);
        xSemaphoreGive(configMutex);
    } else {
        Serial.println("[LOGGER] ❌ Échec prise mutex config");
        return false;
    }

    initialized_ = true;
    return openLogFile();
}

bool Logger::openLogFile() {
    if (xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        hal::IHalStorage& storage = hal::HalManager::instance().storage();
        log_file_ = storage.open("/logs.txt", hal::StorageOpenMode::Append);
        if (!log_file_ || !log_file_->isOpen()) {
            Serial.println("[LOGGER] ❌ Échec ouverture fichier logs");
            xSemaphoreGive(log_mutex_);
            return false;
        }
        xSemaphoreGive(log_mutex_);
        return true;
    }
    return false;
}

void Logger::rotateLogFile() {
    if (xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (log_file_ && log_file_->isOpen() && log_file_->size() > 100000) { // 100 Ko max
            log_file_->close();
            hal::IHalStorage& storage = hal::HalManager::instance().storage();
            storage.remove("/logs.txt");
            log_file_ = storage.open("/logs.txt", hal::StorageOpenMode::Append);
            if (!log_file_ || !log_file_->isOpen()) {
                Serial.println("[LOGGER] ❌ Échec réouverture fichier logs après rotation");
            }
        }
        xSemaphoreGive(log_mutex_);
    }
}

void Logger::log(LogLevel level, const String& message) {
    if (!initialized_ || level > current_level_) return;

    String level_str;
    switch (level) {
        case LOG_ERROR: level_str = "ERROR"; break;
        case LOG_WARNING: level_str = "WARNING"; break;
        case LOG_INFO: level_str = "INFO"; break;
        case LOG_DEBUG: level_str = "DEBUG"; break;
    }

    String log_entry = "[" + String(xTaskGetTickCount() * portTICK_PERIOD_MS) + "] [" + level_str + "] " + message;

    // Log vers Serial
    Serial.println(log_entry);

    // Log vers fichier
    if (xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (log_file_ && log_file_->isOpen()) {
            log_file_->write(reinterpret_cast<const uint8_t*>(log_entry.c_str()), log_entry.length());
            const char newline = '\n';
            log_file_->write(reinterpret_cast<const uint8_t*>(&newline), 1);
            rotateLogFile();
        }
        xSemaphoreGive(log_mutex_);
    }
}

void Logger::setLogLevel(LogLevel level) {
    if (xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        current_level_ = level;
        xSemaphoreGive(log_mutex_);
    }
}

String Logger::getLogs() {
    String logs;
    if (xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        hal::IHalStorage& storage = hal::HalManager::instance().storage();
        auto file = storage.open("/logs.txt", hal::StorageOpenMode::Read);
        if (file && file->isOpen()) {
            std::vector<uint8_t> buffer(file->size());
            size_t read = buffer.empty() ? 0 : file->read(buffer.data(), buffer.size());
            logs.reserve(read + 1);
            logs += String(reinterpret_cast<const char*>(buffer.data()), read);
            file->close();
        }
        xSemaphoreGive(log_mutex_);
    }
    return logs;
}

bool Logger::clearLogs() {
    if (xSemaphoreTake(log_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    hal::IHalStorage& storage = hal::HalManager::instance().storage();
    if (log_file_) {
        log_file_->close();
    }

    storage.remove("/logs.txt");

    log_file_ = storage.open("/logs.txt", hal::StorageOpenMode::Write);
    bool ok = log_file_ && log_file_->isOpen();
    xSemaphoreGive(log_mutex_);
    return ok;
}