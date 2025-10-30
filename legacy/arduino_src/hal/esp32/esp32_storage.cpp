#include "hal/interfaces/ihal_storage.h"

#include <FS.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace hal {

namespace {

class Esp32SpiffsFile : public IHalStorageFile {
public:
    explicit Esp32SpiffsFile(fs::File file) : file_(std::move(file)) {}

    bool isOpen() const override { return file_; }

    size_t read(uint8_t* buffer, size_t length) override {
        return file_.read(buffer, length);
    }

    size_t write(const uint8_t* buffer, size_t length) override {
        return file_.write(buffer, length);
    }

    size_t size() const override { return file_.size(); }

    void close() override {
        if (file_) {
            file_.close();
        }
    }

private:
    fs::File file_;
};

class Esp32NvsFile : public IHalStorageFile {
public:
    Esp32NvsFile(Preferences& prefs, std::string key, StorageOpenMode mode)
        : prefs_(prefs), key_(std::move(key)), mode_(mode) {
        if (mode_ == StorageOpenMode::Read) {
            size_t length = prefs_.getBytesLength(key_.c_str());
            if (length > 0) {
                buffer_.resize(length);
                prefs_.getBytes(key_.c_str(), buffer_.data(), length);
            }
            cursor_ = 0;
        }
    }

    ~Esp32NvsFile() override {
        close();
    }

    bool isOpen() const override { return true; }

    size_t read(uint8_t* buffer, size_t length) override {
        if (mode_ == StorageOpenMode::Write) {
            return 0;
        }
        size_t remaining = buffer_.size() - cursor_;
        size_t to_read = std::min(length, remaining);
        if (to_read > 0) {
            std::memcpy(buffer, buffer_.data() + cursor_, to_read);
            cursor_ += to_read;
        }
        return to_read;
    }

    size_t write(const uint8_t* buffer, size_t length) override {
        if (mode_ == StorageOpenMode::Read) {
            return 0;
        }
        buffer_.insert(buffer_.end(), buffer, buffer + length);
        dirty_ = true;
        return length;
    }

    size_t size() const override {
        return buffer_.size();
    }

    void close() override {
        if (!closed_) {
            if (dirty_) {
                prefs_.putBytes(key_.c_str(), buffer_.data(), buffer_.size());
            }
            closed_ = true;
        }
    }

private:
    Preferences& prefs_;
    std::string key_;
    StorageOpenMode mode_;
    std::vector<uint8_t> buffer_;
    size_t cursor_ = 0;
    bool dirty_ = false;
    bool closed_ = false;
};

std::string sanitizeKey(const std::string& path) {
    std::string key = path;
    std::replace(key.begin(), key.end(), '/', '_');
    if (key.empty()) {
        key = "default";
    }
    return key;
}

} // namespace

class Esp32Storage : public IHalStorage {
public:
    Status mount(const StorageConfig& config) override {
        config_ = config;
        if (config.type == StorageType::SPIFFS) {
            if (!SPIFFS.begin(config.format_on_fail)) {
                return Status::Error;
            }
        } else {
            if (!prefs_.begin("tinybms", false)) {
                return Status::Error;
            }
        }
        mounted_ = true;
        return Status::Ok;
    }

    bool exists(const std::string& path) override {
        if (!mounted_) {
            return false;
        }
        if (config_.type == StorageType::SPIFFS) {
            return SPIFFS.exists(path.c_str());
        }
        return prefs_.isKey(sanitizeKey(path).c_str());
    }

    std::unique_ptr<IHalStorageFile> open(const std::string& path, StorageOpenMode mode) override {
        if (!mounted_) {
            return nullptr;
        }
        if (config_.type == StorageType::SPIFFS) {
            const char* mode_str = (mode == StorageOpenMode::Read) ? "r" : (mode == StorageOpenMode::Write ? "w" : "a");
            fs::File file = SPIFFS.open(path.c_str(), mode_str);
            if (!file) {
                return nullptr;
            }
            return std::make_unique<Esp32SpiffsFile>(std::move(file));
        }
        return std::make_unique<Esp32NvsFile>(prefs_, sanitizeKey(path), mode);
    }

    bool remove(const std::string& path) override {
        if (!mounted_) {
            return false;
        }
        if (config_.type == StorageType::SPIFFS) {
            return SPIFFS.remove(path.c_str());
        }
        return prefs_.remove(sanitizeKey(path).c_str());
    }

private:
    StorageConfig config_{};
    bool mounted_ = false;
    Preferences prefs_;
};

std::unique_ptr<IHalStorage> createEsp32Storage() {
    return std::make_unique<Esp32Storage>();
}

} // namespace hal
