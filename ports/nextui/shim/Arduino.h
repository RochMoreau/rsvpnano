#pragma once
// Arduino core compatibility layer for Linux builds of RSVP Nano (NextUI / desktop).
// Only the API surface used by the firmware and Arduino_GFX is provided.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "pgmspace.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#ifndef ARDUINO_ARCH_LINUX
#define ARDUINO_ARCH_LINUX 1
#endif

using byte = uint8_t;
using boolean = bool;
using word = uint16_t;

#define HIGH 0x1
#define LOW 0x0
#define INPUT 0x01
#define OUTPUT 0x03
#define INPUT_PULLUP 0x05
#define INPUT_PULLDOWN 0x09
#define OUTPUT_OPEN_DRAIN 0x13
#define RISING 0x01
#define FALLING 0x02
#define CHANGE 0x03
#define LSBFIRST 0
#define MSBFIRST 1
#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#ifndef _BV
#define _BV(b) (1UL << (b))
#endif
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define lowByte(w) ((uint8_t) ((w) & 0xff))
#define highByte(w) ((uint8_t) ((w) >> 8))

#define IRAM_ATTR
#define DRAM_ATTR
#define EXT_RAM_BSS_ATTR
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#define NOINLINE_ATTR __attribute__((noinline))

using std::max;
using std::min;

template<typename T, typename L, typename H>
constexpr auto constrain(T value, L low, H high) {
    return value < low ? low : (value > high ? high : value);
}

inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin)
        return outMin;
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
void yield();

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) {
    return HIGH;
}
inline int analogRead(int) {
    return 0;
}
inline void analogWrite(int, int) {}
inline void attachInterrupt(int, void (*)(), int) {}
inline void attachInterruptArg(int, void (*)(void*), void*, int) {}
inline void detachInterrupt(int) {}
inline int digitalPinToInterrupt(int pin) {
    return pin;
}
inline uint32_t analogReadMilliVolts(int) {
    return 0;
}

long random(long howBig);
long random(long howSmall, long howBig);
void randomSeed(unsigned long seed);
inline uint32_t esp_random() {
    return static_cast<uint32_t>(random(0, 0x7fffffff)) ^ (static_cast<uint32_t>(random(0, 0xffff)) << 16);
}

class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(string_literal))
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper*>(p))

#include "WString.h"
#include "Print.h"
#include "Stream.h"
#include "HardwareSerial.h"

class EspClass {
public:
    void restart();
    uint32_t getFreeHeap();
    uint32_t getHeapSize();
    uint32_t getMinFreeHeap() {
        return getFreeHeap();
    }
    uint32_t getMaxAllocHeap() {
        return getFreeHeap();
    }
    uint32_t getFreePsram() {
        return getFreeHeap();
    }
    uint32_t getPsramSize() {
        return getHeapSize();
    }
    const char* getChipModel() {
        return "Linux";
    }
    uint8_t getChipRevision() {
        return 0;
    }
    uint8_t getChipCores() {
        return 4;
    }
    uint32_t getCpuFreqMHz() {
        return 1800;
    }
    uint64_t getEfuseMac();
    const char* getSdkVersion() {
        return "linux";
    }
    uint32_t getFlashChipSize() {
        return 0;
    }
};
extern EspClass ESP;

inline bool psramFound() {
    return true;
}
inline void* ps_malloc(size_t size) {
    return std::malloc(size);
}
inline void* ps_calloc(size_t n, size_t size) {
    return std::calloc(n, size);
}
inline void* ps_realloc(void* p, size_t size) {
    return std::realloc(p, size);
}

inline uint32_t getCpuFrequencyMhz() {
    return 1800;
}
inline bool setCpuFrequencyMhz(uint32_t) {
    return true;
}
