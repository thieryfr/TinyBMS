/**
 * @file esp32_storage_idf.cpp
 * @brief ESP-IDF native Storage (SPIFFS) HAL implementation
 *
 * Phase 1: Fondations ESP-IDF
 * Implements IHalStorage using ESP-IDF SPIFFS + VFS
 */

#include "esp32_storage_idf.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <sys/stat.h>
#include <unistd.h>

namespace hal {

static const char* TAG = "ESP32StorageIDF";

// ============================================================================
// ESP32StorageFileIDF Implementation
// ============================================================================

ESP32StorageFileIDF::ESP32StorageFileIDF(const std::string& path, StorageOpenMode mode)
    : path_(path)
    , mode_(mode) {

    std::ios_base::openmode open_mode;
    switch (mode) {
        case StorageOpenMode::Read:
            open_mode = std::ios::in | std::ios::binary;
            break;
        case StorageOpenMode::Write:
            open_mode = std::ios::out | std::ios::binary | std::ios::trunc;
            break;
        case StorageOpenMode::Append:
            open_mode = std::ios::out | std::ios::binary | std::ios::app;
            break;
        default:
            open_mode = std::ios::in | std::ios::binary;
    }

    file_.open(path, open_mode);
    if (!file_.is_open()) {
        ESP_LOGW(TAG, "Failed to open file: %s", path.c_str());
    }
}

ESP32StorageFileIDF::~ESP32StorageFileIDF() {
    close();
}

bool ESP32StorageFileIDF::isOpen() const {
    return file_.is_open();
}

size_t ESP32StorageFileIDF::read(uint8_t* buffer, size_t length) {
    if (!file_.is_open() || !buffer) {
        return 0;
    }

    file_.read(reinterpret_cast<char*>(buffer), length);
    return file_.gcount();
}

size_t ESP32StorageFileIDF::write(const uint8_t* buffer, size_t length) {
    if (!file_.is_open() || !buffer) {
        return 0;
    }

    file_.write(reinterpret_cast<const char*>(buffer), length);
    return file_.good() ? length : 0;
}

size_t ESP32StorageFileIDF::size() const {
    if (!file_.is_open()) {
        return 0;
    }

    // Get file size using stat
    struct stat st;
    if (stat(path_.c_str(), &st) == 0) {
        return st.st_size;
    }

    return 0;
}

void ESP32StorageFileIDF::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

// ============================================================================
// ESP32StorageIDF Implementation
// ============================================================================

ESP32StorageIDF::ESP32StorageIDF()
    : mounted_(false)
    , config_{} {
}

ESP32StorageIDF::~ESP32StorageIDF() {
    if (mounted_) {
        esp_vfs_spiffs_unregister(nullptr);
    }
}

Status ESP32StorageIDF::mount(const StorageConfig& config) {
    if (config.type != StorageType::SPIFFS) {
        ESP_LOGE(TAG, "Only SPIFFS supported in Phase 1");
        return Status::Unsupported;
    }

    // Check if already mounted with same config (idempotent)
    if (mounted_) {
        bool config_changed = (config_.format_on_fail != config.format_on_fail);

        if (!config_changed) {
            ESP_LOGD(TAG, "SPIFFS already mounted, skipping");
            return Status::Ok;
        }

        // Config changed, need to remount
        ESP_LOGI(TAG, "SPIFFS config changed, remounting...");
        esp_vfs_spiffs_unregister(nullptr);
        mounted_ = false;
    }

    config_ = config;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = BASE_PATH,
        .partition_label = nullptr,  // Use default partition
        .max_files = 5,
        .format_if_mount_failed = config.format_on_fail
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount SPIFFS");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "SPIFFS partition not found");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS: %s", esp_err_to_name(ret));
        }
        return Status::Error;
    }

    // Get partition info
    size_t total = 0, used = 0;
    ret = esp_spiffs_info(nullptr, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS mounted: total=%zu KB, used=%zu KB",
                 total / 1024, used / 1024);
    }

    mounted_ = true;
    return Status::Ok;
}

bool ESP32StorageIDF::exists(const std::string& path) {
    std::string full_path = std::string(BASE_PATH) + path;
    struct stat st;
    return stat(full_path.c_str(), &st) == 0;
}

std::unique_ptr<IHalStorageFile> ESP32StorageIDF::open(const std::string& path, StorageOpenMode mode) {
    if (!mounted_) {
        ESP_LOGW(TAG, "Storage not mounted");
        return nullptr;
    }

    std::string full_path = std::string(BASE_PATH) + path;
    auto file = std::make_unique<ESP32StorageFileIDF>(full_path, mode);

    if (!file->isOpen()) {
        return nullptr;
    }

    return file;
}

bool ESP32StorageIDF::remove(const std::string& path) {
    if (!mounted_) {
        return false;
    }

    std::string full_path = std::string(BASE_PATH) + path;
    return unlink(full_path.c_str()) == 0;
}

} // namespace hal
