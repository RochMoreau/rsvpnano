#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include "WString.h"

class SHA256Builder {
public:
    void begin();
    void add(const uint8_t* data, size_t length);
    void add(const char* data) { add(reinterpret_cast<const uint8_t*>(data), std::strlen(data)); }
    void calculate();
    void getBytes(uint8_t* output) const;
    String toString() const;

private:
    void block(const uint8_t* chunk);
    std::array<uint32_t, 8> state_{};
    std::array<uint8_t, 64> buffer_{};
    uint64_t bits_ = 0;
    size_t used_ = 0;
    std::array<uint8_t, 32> digest_{};
};
