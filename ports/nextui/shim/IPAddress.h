#pragma once
#include <cstdint>
#include <cstdio>
#include "WString.h"
class IPAddress {
public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : b_{a, b, c, d} {}
    uint8_t operator[](int i) const { return b_[i]; }
    String toString() const {
        char s[16];
        std::snprintf(s, sizeof s, "%u.%u.%u.%u", b_[0], b_[1], b_[2], b_[3]);
        return String(s);
    }
    explicit operator uint32_t() const { return b_[0] | (b_[1] << 8) | (b_[2] << 16) | (uint32_t(b_[3]) << 24); }

private:
    uint8_t b_[4] = {};
};
