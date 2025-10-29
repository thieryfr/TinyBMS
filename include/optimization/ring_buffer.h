#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace optimization {

class ByteRingBuffer {
public:
    explicit ByteRingBuffer(size_t capacity = 256);

    size_t capacity() const;
    size_t size() const;
    bool empty() const;
    bool full() const;

    void clear();

    size_t push(const uint8_t* data, size_t length);
    size_t pop(uint8_t* destination, size_t length);
    size_t peek(uint8_t* destination, size_t length) const;

private:
    std::vector<uint8_t> buffer_;
    size_t head_ = 0;
    size_t tail_ = 0;
    bool full_flag_ = false;
};

}  // namespace optimization
