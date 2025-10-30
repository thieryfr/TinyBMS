#pragma once

#include "esp_err.h"
#include <cstdint>
#include <string>
#include <vector>

namespace tinybms::log {

enum class Level : uint8_t {
    None = 0,
    Error,
    Warn,
    Info,
    Debug,
    Verbose,
};

struct Entry {
    uint64_t timestamp_ms;
    Level level;
    std::string tag;
    std::string message;
};

void init();
void shutdown();
void append(Level level, const char *tag, const char *message);
std::vector<Entry> recent(size_t max_entries);

Level level_from_string(const std::string &value);
std::string level_to_string(Level level);

esp_err_t set_global_level(Level level);
Level current_level();

} // namespace tinybms::log
