#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "hal/hal_config.h"
#include "hal/hal_types.h"

namespace hal {

class IHalStorageFile {
public:
    virtual ~IHalStorageFile() = default;
    virtual bool isOpen() const = 0;
    virtual size_t read(uint8_t* buffer, size_t length) = 0;
    virtual size_t write(const uint8_t* buffer, size_t length) = 0;
    virtual size_t size() const = 0;
    virtual void close() = 0;
};

class IHalStorage {
public:
    virtual ~IHalStorage() = default;

    virtual Status mount(const StorageConfig& config) = 0;
    virtual bool exists(const std::string& path) = 0;
    virtual std::unique_ptr<IHalStorageFile> open(const std::string& path, StorageOpenMode mode) = 0;
    virtual bool remove(const std::string& path) = 0;
};

} // namespace hal
