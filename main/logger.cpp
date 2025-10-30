#include "logger.hpp"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iterator>

namespace tinybms::log {
namespace {
constexpr size_t kMaxEntries = 256;
SemaphoreHandle_t log_lock = nullptr;
std::deque<Entry> entries;
vprintf_like_t previous_vprintf = nullptr;
Level global_level = Level::Info;

Level level_from_prefix(char prefix) {
    switch (prefix) {
        case 'E':
            return Level::Error;
        case 'W':
            return Level::Warn;
        case 'I':
            return Level::Info;
        case 'D':
            return Level::Debug;
        case 'V':
            return Level::Verbose;
        default:
            return Level::Info;
    }
}

esp_log_level_t to_esp(Level level) {
    switch (level) {
        case Level::Error:
            return ESP_LOG_ERROR;
        case Level::Warn:
            return ESP_LOG_WARN;
        case Level::Info:
            return ESP_LOG_INFO;
        case Level::Debug:
            return ESP_LOG_DEBUG;
        case Level::Verbose:
            return ESP_LOG_VERBOSE;
        default:
            return ESP_LOG_NONE;
    }
}

Level from_esp(esp_log_level_t level) {
    switch (level) {
        case ESP_LOG_ERROR:
            return Level::Error;
        case ESP_LOG_WARN:
            return Level::Warn;
        case ESP_LOG_INFO:
            return Level::Info;
        case ESP_LOG_DEBUG:
            return Level::Debug;
        case ESP_LOG_VERBOSE:
            return Level::Verbose;
        default:
            return Level::None;
    }
}

void store_line(const char *line) {
    if (!log_lock) {
        return;
    }

    const char *cursor = line;
    while (*cursor == '\x1B') {
        const char *m = std::strchr(cursor, 'm');
        if (!m) {
            break;
        }
        cursor = m + 1;
    }

    Level level = level_from_prefix(cursor[0]);
    const char *after_level = cursor;
    if (*after_level != '\0') {
        after_level++;
    }

    std::string tag;
    std::string message;

    const char *close_paren = std::strchr(after_level, ')');
    if (close_paren) {
        const char *tag_start = close_paren + 2; // skip ") "
        const char *colon = std::strstr(tag_start, ":");
        if (colon) {
            tag.assign(tag_start, colon - tag_start);
            const char *msg_start = colon + 1;
            if (*msg_start == ' ') {
                msg_start++;
            }
            message = msg_start;
        }
    }

    if (message.empty()) {
        message = cursor;
    }

    message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
    message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());

    if (xSemaphoreTake(log_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    Entry entry{};
    entry.timestamp_ms = esp_timer_get_time() / 1000ULL;
    entry.level = level;
    entry.tag = tag.empty() ? "" : tag;
    entry.message = message;

    entries.push_back(std::move(entry));
    while (entries.size() > kMaxEntries) {
        entries.pop_front();
    }
    xSemaphoreGive(log_lock);
}

int log_vprintf(const char *fmt, va_list args) {
    char buffer[512];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(buffer, sizeof(buffer), fmt, copy);
    va_end(copy);

    store_line(buffer);

    if (previous_vprintf) {
        return previous_vprintf(fmt, args);
    }
    return vprintf(fmt, args);
}

} // namespace

void init() {
    if (!log_lock) {
        log_lock = xSemaphoreCreateMutex();
    }
    entries.clear();
    previous_vprintf = esp_log_set_vprintf(&log_vprintf);
    global_level = Level::Info;
}

void shutdown() {
    if (previous_vprintf) {
        esp_log_set_vprintf(previous_vprintf);
        previous_vprintf = nullptr;
    }
    if (log_lock) {
        vSemaphoreDelete(log_lock);
        log_lock = nullptr;
    }
    entries.clear();
}

void append(Level level, const char *tag, const char *message) {
    if (!tag) {
        tag = "";
    }
    if (!message) {
        message = "";
    }

    if (xSemaphoreTake(log_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return;
    }

    Entry entry{};
    entry.timestamp_ms = esp_timer_get_time() / 1000ULL;
    entry.level = level;
    entry.tag = tag;
    entry.message = message;

    entries.push_back(std::move(entry));
    while (entries.size() > kMaxEntries) {
        entries.pop_front();
    }
    xSemaphoreGive(log_lock);

    esp_log_level_t esp_level = to_esp(level);
    if (esp_level != ESP_LOG_NONE) {
        const char *level_tag = tag[0] ? tag : "tinybms";
        esp_log_write(esp_level, level_tag, "%s", message);
    }
}

std::vector<Entry> recent(size_t max_entries) {
    std::vector<Entry> snapshot;
    if (xSemaphoreTake(log_lock, pdMS_TO_TICKS(10)) != pdTRUE) {
        return snapshot;
    }
    size_t count = std::min(max_entries, entries.size());
    snapshot.reserve(count);
    auto begin = entries.size() > count ? entries.end() - count : entries.begin();
    for (auto it = begin; it != entries.end(); ++it) {
        snapshot.push_back(*it);
    }
    xSemaphoreGive(log_lock);
    return snapshot;
}

Level level_from_string(const std::string &value) {
    std::string lower;
    lower.reserve(value.size());
    std::transform(value.begin(), value.end(), std::back_inserter(lower), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "none") {
        return Level::None;
    }
    if (lower == "error") {
        return Level::Error;
    }
    if (lower == "warn" || lower == "warning") {
        return Level::Warn;
    }
    if (lower == "debug") {
        return Level::Debug;
    }
    if (lower == "verbose") {
        return Level::Verbose;
    }
    return Level::Info;
}

std::string level_to_string(Level level) {
    switch (level) {
        case Level::None:
            return "none";
        case Level::Error:
            return "error";
        case Level::Warn:
            return "warn";
        case Level::Info:
            return "info";
        case Level::Debug:
            return "debug";
        case Level::Verbose:
            return "verbose";
        default:
            return "info";
    }
}

esp_err_t set_global_level(Level level) {
    global_level = level;
    return esp_log_level_set("*", to_esp(level));
}

Level current_level() {
    return global_level;
}

} // namespace tinybms::log
