/**
 * @file esp32_storage_idf.h
 * @brief ESP-IDF native Storage (SPIFFS) HAL implementation header
 *
 * Phase 1: Fondations ESP-IDF
 */

#pragma once

#include "hal/interfaces/ihal_storage.h"
#include <fstream>

namespace hal {

class ESP32StorageFileIDF : public IHalStorageFile {
public:
    ESP32StorageFileIDF(const std::string& path, StorageOpenMode mode);
    ~ESP32StorageFileIDF() override;

    bool isOpen() const override;
    size_t read(uint8_t* buffer, size_t length) override;
    size_t write(const uint8_t* buffer, size_t length) override;
    size_t size() const override;
    void close() override;

private:
    std::fstream file_;
    std::string path_;
    StorageOpenMode mode_;
};

class ESP32StorageIDF : public IHalStorage {
public:
    ESP32StorageIDF();
    ~ESP32StorageIDF() override;

    Status mount(const StorageConfig& config) override;
    bool exists(const std::string& path) override;
    std::unique_ptr<IHalStorageFile> open(const std::string& path, StorageOpenMode mode) override;
    bool remove(const std::string& path) override;

private:
    bool mounted_;
    StorageConfig config_;
    static constexpr const char* BASE_PATH = "/spiffs";
};

} // namespace hal
