#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <type_traits>

#define HEX 16
#define DEC 10

using byte = uint8_t;

namespace arduino_stub {
inline uint32_t current_millis = 0;

inline void resetMillis(uint32_t value = 0) {
    current_millis = value;
}

inline void advanceMillis(uint32_t delta) {
    current_millis += delta;
}
} // namespace arduino_stub

inline uint32_t millis() {
    return arduino_stub::current_millis;
}

inline void delay(uint32_t) {}

class String {
public:
    String() = default;

    String(const char* s) : data_(s ? s : "") {}

    String(const std::string& s) : data_(s) {}

    String(char c) : data_(1, c) {}

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
    explicit String(T value) {
        data_ = std::to_string(static_cast<long long>(value));
    }

    String(double value, unsigned char decimals = 2) {
        data_ = formatFloat(value, decimals);
    }

    String(unsigned int value, unsigned char base) {
        data_ = toBaseString(static_cast<uint64_t>(value), base);
    }

    String(unsigned long value, unsigned char base) {
        data_ = toBaseString(static_cast<uint64_t>(value), base);
    }

    String(int value, unsigned char base) {
        if (value < 0) {
            data_ = "-" + toBaseString(static_cast<uint64_t>(-static_cast<long long>(value)), base);
        } else {
            data_ = toBaseString(static_cast<uint64_t>(value), base);
        }
    }

    String(long value, unsigned char base) {
        if (value < 0) {
            data_ = "-" + toBaseString(static_cast<uint64_t>(-value), base);
        } else {
            data_ = toBaseString(static_cast<uint64_t>(value), base);
        }
    }

    String(const String&) = default;
    String(String&&) noexcept = default;
    String& operator=(const String&) = default;
    String& operator=(String&&) noexcept = default;

    String& operator+=(const String& rhs) {
        data_ += rhs.data_;
        return *this;
    }

    String& operator+=(const std::string& rhs) {
        data_ += rhs;
        return *this;
    }

    String& operator+=(const char* rhs) {
        data_ += (rhs ? rhs : "");
        return *this;
    }

    String& operator+=(char rhs) {
        data_.push_back(rhs);
        return *this;
    }

    template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> 
    String& operator+=(T value) {
        data_ += std::to_string(static_cast<long long>(value));
        return *this;
    }

    String& operator+=(float value) {
        data_ += formatFloat(static_cast<double>(value), 2);
        return *this;
    }

    String& operator+=(double value) {
        data_ += formatFloat(value, 2);
        return *this;
    }

    friend String operator+(String lhs, const String& rhs) {
        lhs += rhs;
        return lhs;
    }

    friend String operator+(String lhs, const char* rhs) {
        lhs += rhs;
        return lhs;
    }

    friend String operator+(const char* lhs, const String& rhs) {
        String tmp(lhs);
        tmp += rhs;
        return tmp;
    }

    const char* c_str() const {
        return data_.c_str();
    }

    std::string toStdString() const {
        return data_;
    }

    void reserve(size_t) {}

    size_t length() const {
        return data_.size();
    }

private:
    static std::string formatFloat(double value, unsigned char decimals) {
        std::ostringstream oss;
        oss.setf(std::ios::fixed, std::ios::floatfield);
        oss << std::setprecision(decimals) << value;
        return oss.str();
    }

    template <typename T>
    static std::string toBaseString(T value, unsigned char base) {
        if (base == HEX) {
            std::ostringstream oss;
            oss << std::uppercase << std::hex << static_cast<uint64_t>(value);
            return oss.str();
        }
        return std::to_string(static_cast<long long>(value));
    }

    std::string data_;
};

class SerialClass {
public:
    SerialClass() = default;

    template <typename T>
    void print(const T&) {}

    template <typename T>
    void println(const T&) {}

    void println() {}

    template <typename... Args>
    void printf(const char*, Args&&...) {}
};

inline SerialClass Serial;

inline void yield() {}

