#include "hal/interfaces/ihal_storage.h"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hal {

class MockStorageFile : public IHalStorageFile {
public:
    MockStorageFile(std::vector<uint8_t>& backing, StorageOpenMode mode)
        : backing_(backing), mode_(mode) {
        if (mode_ == StorageOpenMode::Read) {
            cursor_ = 0;
        } else {
            backing_.clear();
        }
    }

    bool isOpen() const override { return true; }

    size_t read(uint8_t* buffer, size_t length) override {
        if (mode_ == StorageOpenMode::Write) {
            return 0;
        }
        size_t remaining = backing_.size() - cursor_;
        size_t to_read = std::min(length, remaining);
        if (to_read > 0) {
            std::copy(backing_.begin() + cursor_, backing_.begin() + cursor_ + to_read, buffer);
            cursor_ += to_read;
        }
        return to_read;
    }

    size_t write(const uint8_t* buffer, size_t length) override {
        if (mode_ == StorageOpenMode::Read) {
            return 0;
        }
        backing_.insert(backing_.end(), buffer, buffer + length);
        return length;
    }

    size_t size() const override { return backing_.size(); }

    void close() override {}

private:
    std::vector<uint8_t>& backing_;
    StorageOpenMode mode_;
    size_t cursor_ = 0;
};

class MockStorage : public IHalStorage {
public:
    Status mount(const StorageConfig& config) override {
        config_ = config;
        mounted_ = true;
        return Status::Ok;
    }

    bool exists(const std::string& path) override {
        return files_.count(path) > 0;
    }

    std::unique_ptr<IHalStorageFile> open(const std::string& path, StorageOpenMode mode) override {
        if (!mounted_) {
            return nullptr;
        }
        auto& data = files_[path];
        return std::make_unique<MockStorageFile>(data, mode);
    }

    bool remove(const std::string& path) override {
        return files_.erase(path) > 0;
    }

private:
    StorageConfig config_{};
    bool mounted_ = false;
    std::map<std::string, std::vector<uint8_t>> files_;
};

std::unique_ptr<IHalStorage> createMockStorage() {
    return std::make_unique<MockStorage>();
}

} // namespace hal
