/**
 * @file esp32_storage_idf.cpp
 * @brief ESP-IDF native SPIFFS Storage HAL for PlatformIO
 *
 * Phase 2: Migration Périphériques
 */

#include "hal/interfaces/ihal_storage.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include <memory>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

namespace hal {

static const char* TAG = "ESP32StorageIDF";
static constexpr const char* BASE_PATH = "/spiffs";

class ESP32StorageFileIDF : public IHalStorageFile {
public:
    ESP32StorageFileIDF(const std::string& path, StorageOpenMode mode)
        : path_(path), mode_(mode) {

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
    }

    ~ESP32StorageFileIDF() override { close(); }

    bool isOpen() const override { return file_.is_open(); }

    size_t read(uint8_t* buffer, size_t length) override {
        if (!file_.is_open() || !buffer) return 0;
        file_.read(reinterpret_cast<char*>(buffer), length);
        return file_.gcount();
    }

    size_t write(const uint8_t* buffer, size_t length) override {
        if (!file_.is_open() || !buffer) return 0;
        file_.write(reinterpret_cast<const char*>(buffer), length);
        return file_.good() ? length : 0;
    }

    size_t size() const override {
        struct stat st;
        return (stat(path_.c_str(), &st) == 0) ? st.st_size : 0;
    }

    void close() override {
        if (file_.is_open()) {
            file_.close();
        }
    }

private:
    std::fstream file_;
    std::string path_;
    StorageOpenMode mode_;
};

class ESP32StorageIDF : public IHalStorage {
public:
    ESP32StorageIDF() : mounted_(false), config_{} {}

    ~ESP32StorageIDF() override {
        if (mounted_) {
            esp_vfs_spiffs_unregister(nullptr);
        }
    }

    Status mount(const StorageConfig& config) override {
        config_ = config;

        if (config.type != StorageType::SPIFFS) {
            ESP_LOGE(TAG, "Only SPIFFS supported");
            return Status::Unsupported;
        }

        esp_vfs_spiffs_conf_t conf = {
            .base_path = BASE_PATH,
            .partition_label = nullptr,
            .max_files = 5,
            .format_if_mount_failed = config.format_on_fail
        };

        esp_err_t ret = esp_vfs_spiffs_register(&conf);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
            return Status::Error;
        }

        size_t total = 0, used = 0;
        if (esp_spiffs_info(nullptr, &total, &used) == ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS: total=%zu KB, used=%zu KB",
                     total / 1024, used / 1024);
        }

        mounted_ = true;
        return Status::Ok;
    }

    bool exists(const std::string& path) override {
        std::string full_path = std::string(BASE_PATH) + path;
        struct stat st;
        return stat(full_path.c_str(), &st) == 0;
    }

    std::unique_ptr<IHalStorageFile> open(const std::string& path, StorageOpenMode mode) override {
        if (!mounted_) return nullptr;

        std::string full_path = std::string(BASE_PATH) + path;
        auto file = std::make_unique<ESP32StorageFileIDF>(full_path, mode);

        return file->isOpen() ? std::move(file) : nullptr;
    }

    bool remove(const std::string& path) override {
        if (!mounted_) return false;
        std::string full_path = std::string(BASE_PATH) + path;
        return unlink(full_path.c_str()) == 0;
    }

private:
    bool mounted_;
    StorageConfig config_;
};

std::unique_ptr<IHalStorage> createEsp32IdfStorage() {
    return std::make_unique<ESP32StorageIDF>();
}

} // namespace hal
