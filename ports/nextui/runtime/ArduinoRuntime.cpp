// Arduino / ESP-IDF runtime services for Linux hosts.
#include <Arduino.h>
#include <IPAddress.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <mutex>
#include <random>
#include <string>
#include <thread>

#include <sys/sysinfo.h>
#include <unistd.h>

#include <RsvpRuntime.h>

namespace {
    const auto gStart = std::chrono::steady_clock::now();
    std::mutex gLogMutex;
    std::mutex gRandomMutex;
    std::mt19937 gRandom{static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    esp_log_level_t gLogLevel = ESP_LOG_INFO;
    FILE* gLogFile = nullptr;

    char levelLetter(esp_log_level_t level) {
        switch (level) {
        case ESP_LOG_ERROR:
            return 'E';
        case ESP_LOG_WARN:
            return 'W';
        case ESP_LOG_INFO:
            return 'I';
        case ESP_LOG_DEBUG:
            return 'D';
        default:
            return 'V';
        }
    }

    void writeLogLine(const char* text, size_t length) {
        std::fwrite(text, 1, length, stderr);
        if (gLogFile != nullptr) {
            std::fwrite(text, 1, length, gLogFile);
            std::fflush(gLogFile);
        }
    }
} // namespace

namespace rsvp_runtime {
    void setLogLevel(int level) {
        gLogLevel = static_cast<esp_log_level_t>(std::clamp(level, 0, 5));
    }
    void setLogFile(const char* path) {
        const std::lock_guard lock{gLogMutex};
        if (gLogFile != nullptr)
            std::fclose(gLogFile);
        gLogFile = path != nullptr ? std::fopen(path, "w") : nullptr;
    }
} // namespace rsvp_runtime

unsigned long millis() {
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - gStart).count());
}

unsigned long micros() {
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - gStart).count());
}

void delay(unsigned long ms) {
    if (ms == 0) {
        std::this_thread::yield();
        return;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void delayMicroseconds(unsigned int us) {
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void yield() {
    std::this_thread::yield();
}

long random(long howBig) {
    if (howBig <= 0)
        return 0;
    return random(0, howBig);
}

long random(long howSmall, long howBig) {
    if (howSmall >= howBig)
        return howSmall;
    const std::lock_guard lock{gRandomMutex};
    std::uniform_int_distribution<long> distribution(howSmall, howBig - 1);
    return distribution(gRandom);
}

void randomSeed(unsigned long seed) {
    if (seed == 0)
        return;
    const std::lock_guard lock{gRandomMutex};
    gRandom.seed(static_cast<uint32_t>(seed));
}

// ---------------------------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------------------------
void rsvp_log_write(esp_log_level_t level, const char* tag, const char* format, ...) {
    if (level > gLogLevel)
        return;
    char message[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    char line[1200];
    const int length = std::snprintf(line, sizeof(line), "%c (%lu) %s: %s\n", levelLetter(level), millis(),
                                     tag != nullptr ? tag : "-", message);
    const std::lock_guard lock{gLogMutex};
    writeLogLine(line, static_cast<size_t>(std::clamp(length, 0, static_cast<int>(sizeof(line) - 1))));
}

void esp_log_level_set(const char*, esp_log_level_t) {}

uint32_t esp_log_timestamp() {
    return static_cast<uint32_t>(millis());
}

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func) {
    return func;
}

int esp_rom_printf(const char* format, ...) {
    char message[1024];
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    const std::lock_guard lock{gLogMutex};
    writeLogLine(message, std::strlen(message));
    return length;
}

const char* esp_err_to_name(esp_err_t code) {
    switch (code) {
    case ESP_OK:
        return "ESP_OK";
    case ESP_FAIL:
        return "ESP_FAIL";
    case ESP_ERR_NO_MEM:
        return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:
        return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:
        return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:
        return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:
        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:
        return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:
        return "ESP_ERR_TIMEOUT";
    default:
        return "ESP_ERR_UNKNOWN";
    }
}

// ---------------------------------------------------------------------------------------------
// Memory and system
// ---------------------------------------------------------------------------------------------
size_t heap_caps_get_free_size(uint32_t) {
    struct sysinfo info{};
    if (sysinfo(&info) != 0)
        return 64U * 1024U * 1024U;
    const unsigned long long bytes = static_cast<unsigned long long>(info.freeram) * info.mem_unit;
    // The firmware reasons in 32-bit heap sizes; clamp so arithmetic never wraps.
    return static_cast<size_t>(std::min<unsigned long long>(bytes, 0x7fffffffULL));
}

uint32_t esp_get_free_heap_size() {
    return static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
}

void esp_restart() {
    rsvp_runtime::restartRequested();
    // The app thread must not continue past a restart request.
    while (true)
        std::this_thread::sleep_for(std::chrono::hours(1));
}

EspClass ESP;

void EspClass::restart() {
    esp_restart();
}

uint32_t EspClass::getFreeHeap() {
    return esp_get_free_heap_size();
}

uint32_t EspClass::getHeapSize() {
    struct sysinfo info{};
    if (sysinfo(&info) != 0)
        return 128U * 1024U * 1024U;
    return static_cast<uint32_t>(
        std::min<unsigned long long>(static_cast<unsigned long long>(info.totalram) * info.mem_unit, 0x7fffffffULL));
}

uint64_t EspClass::getEfuseMac() {
    // Stable per-device identifier derived from the machine id when available.
    std::ifstream file("/etc/machine-id");
    std::string id;
    std::getline(file, id);
    uint64_t hash = 1469598103934665603ULL;
    for (const char c: id) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash & 0xffffffffffffULL;
}

// ---------------------------------------------------------------------------------------------
// Serial console and peripheral singletons
// ---------------------------------------------------------------------------------------------
HardwareSerial Serial;
HardwareSerial Serial0;

size_t HardwareSerial::write(uint8_t c) {
    return write(&c, 1);
}

size_t HardwareSerial::write(const uint8_t* buffer, size_t size) {
    const std::lock_guard lock{gLogMutex};
    writeLogLine(reinterpret_cast<const char*>(buffer), size);
    return size;
}

void HardwareSerial::flush() {
    std::fflush(stderr);
}

TwoWire Wire{0};
TwoWire Wire1{1};
SPIClass SPI;
WiFiClass WiFi;
