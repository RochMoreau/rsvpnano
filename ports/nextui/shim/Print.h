#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "WString.h"

class __FlashStringHelper;
class Printable;

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t value) = 0;
    virtual size_t write(const uint8_t* buffer, size_t size) {
        size_t n = 0;
        while (size--) {
            if (write(*buffer++)) ++n;
            else break;
        }
        return n;
    }
    size_t write(const char* str) { return str ? write(reinterpret_cast<const uint8_t*>(str), std::strlen(str)) : 0; }
    size_t write(const char* buffer, size_t size) { return write(reinterpret_cast<const uint8_t*>(buffer), size); }
    virtual int availableForWrite() { return 0; }
    virtual void flush() {}

    size_t printf(const char* format, ...) __attribute__((format(printf, 2, 3))) {
        va_list args;
        va_start(args, format);
        va_list copy;
        va_copy(copy, args);
        const int length = std::vsnprintf(nullptr, 0, format, copy);
        va_end(copy);
        if (length <= 0) { va_end(args); return 0; }
        std::vector<char> buffer(static_cast<size_t>(length) + 1);
        std::vsnprintf(buffer.data(), buffer.size(), format, args);
        va_end(args);
        return write(reinterpret_cast<const uint8_t*>(buffer.data()), static_cast<size_t>(length));
    }

    size_t print(const __FlashStringHelper* s) { return write(reinterpret_cast<const char*>(s)); }
    size_t print(const String& s) { return write(s.c_str(), s.length()); }
    size_t print(const std::string& s) { return write(s.data(), s.size()); }
    size_t print(const char* s) { return write(s); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    size_t print(unsigned char v, int base = 10) { return print(static_cast<unsigned long>(v), base); }
    size_t print(int v, int base = 10) { return print(static_cast<long>(v), base); }
    size_t print(unsigned int v, int base = 10) { return print(static_cast<unsigned long>(v), base); }
    size_t print(long v, int base = 10) { return base == 10 ? printf("%ld", v) : print(static_cast<unsigned long>(v), base); }
    size_t print(unsigned long v, int base = 10) {
        if (base == 16) return printf("%lx", v);
        if (base == 8) return printf("%lo", v);
        return printf("%lu", v);
    }
    size_t print(long long v, int base = 10) { return base == 10 ? printf("%lld", v) : printf("%llx", static_cast<unsigned long long>(v)); }
    size_t print(unsigned long long v, int base = 10) { return base == 16 ? printf("%llx", v) : printf("%llu", v); }
    size_t print(double v, int digits = 2) { return printf("%.*f", digits, v); }

    template<typename T>
    size_t println(const T& v) { const size_t n = print(v); return n + println(); }
    template<typename T>
    size_t println(const T& v, int base) { const size_t n = print(v, base); return n + println(); }
    size_t println() { return write("\r\n"); }
};
