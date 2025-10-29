#include "optimization/ring_buffer.h"

#include <algorithm>
#include <cstring>

namespace optimization {

ByteRingBuffer::ByteRingBuffer(size_t capacity)
    : buffer_(std::max<size_t>(1, capacity)), head_(0), tail_(0), full_flag_(false) {}

size_t ByteRingBuffer::capacity() const {
    return buffer_.size();
}

size_t ByteRingBuffer::size() const {
    if (full_flag_) {
        return buffer_.size();
    }
    if (head_ >= tail_) {
        return head_ - tail_;
    }
    return buffer_.size() - (tail_ - head_);
}

bool ByteRingBuffer::empty() const {
    return (!full_flag_ && head_ == tail_);
}

bool ByteRingBuffer::full() const {
    return full_flag_;
}

void ByteRingBuffer::clear() {
    head_ = tail_ = 0;
    full_flag_ = false;
}

size_t ByteRingBuffer::push(const uint8_t* data, size_t length) {
    if (length == 0 || data == nullptr) {
        return 0;
    }

    size_t written = 0;
    while (written < length) {
        if (full_flag_) {
            break;
        }
        buffer_[head_] = data[written++];
        head_ = (head_ + 1) % buffer_.size();
        if (head_ == tail_) {
            full_flag_ = true;
        }
    }
    return written;
}

size_t ByteRingBuffer::pop(uint8_t* destination, size_t length) {
    if (length == 0 || destination == nullptr || empty()) {
        return 0;
    }

    size_t read = 0;
    while (read < length && !empty()) {
        destination[read++] = buffer_[tail_];
        tail_ = (tail_ + 1) % buffer_.size();
        full_flag_ = false;
    }
    return read;
}

size_t ByteRingBuffer::peek(uint8_t* destination, size_t length) const {
    if (length == 0 || destination == nullptr || empty()) {
        return 0;
    }

    size_t copied = 0;
    size_t index = tail_;
    bool full = full_flag_;
    while (copied < length && (full || index != head_)) {
        destination[copied++] = buffer_[index];
        index = (index + 1) % buffer_.size();
        if (full) {
            if (index == head_) {
                full = false;
            }
        }
    }
    return copied;
}

}  // namespace optimization
