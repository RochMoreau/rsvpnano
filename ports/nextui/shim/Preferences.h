#pragma once
// ESP32 NVS Preferences persisted as one small binary file per namespace.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "WString.h"

class Preferences {
public:
    // Directory holding <namespace>.nvs files; set once at startup by the runtime.
    static void setStorageDirectory(std::string directory);

    bool begin(const char* name, bool readOnly = false, const char* partition = nullptr);
    void end();
    bool clear();
    bool remove(const char* key);
    bool isKey(const char* key);

    size_t putChar(const char* key, int8_t value) { return putValue(key, &value, sizeof value); }
    size_t putUChar(const char* key, uint8_t value) { return putValue(key, &value, sizeof value); }
    size_t putShort(const char* key, int16_t value) { return putValue(key, &value, sizeof value); }
    size_t putUShort(const char* key, uint16_t value) { return putValue(key, &value, sizeof value); }
    size_t putInt(const char* key, int32_t value) { return putValue(key, &value, sizeof value); }
    size_t putUInt(const char* key, uint32_t value) { return putValue(key, &value, sizeof value); }
    size_t putLong(const char* key, int32_t value) { return putValue(key, &value, sizeof value); }
    size_t putULong(const char* key, uint32_t value) { return putValue(key, &value, sizeof value); }
    size_t putLong64(const char* key, int64_t value) { return putValue(key, &value, sizeof value); }
    size_t putULong64(const char* key, uint64_t value) { return putValue(key, &value, sizeof value); }
    size_t putFloat(const char* key, float value) { return putValue(key, &value, sizeof value); }
    size_t putBool(const char* key, bool value) { const uint8_t v = value; return putValue(key, &v, sizeof v); }
    size_t putString(const char* key, const char* value);
    size_t putString(const char* key, const String& value) { return putString(key, value.c_str()); }
    size_t putBytes(const char* key, const void* value, size_t length) { return putValue(key, value, length); }

    int8_t getChar(const char* key, int8_t fallback = 0) { return getValue(key, fallback); }
    uint8_t getUChar(const char* key, uint8_t fallback = 0) { return getValue(key, fallback); }
    int16_t getShort(const char* key, int16_t fallback = 0) { return getValue(key, fallback); }
    uint16_t getUShort(const char* key, uint16_t fallback = 0) { return getValue(key, fallback); }
    int32_t getInt(const char* key, int32_t fallback = 0) { return getValue(key, fallback); }
    uint32_t getUInt(const char* key, uint32_t fallback = 0) { return getValue(key, fallback); }
    int32_t getLong(const char* key, int32_t fallback = 0) { return getValue(key, fallback); }
    uint32_t getULong(const char* key, uint32_t fallback = 0) { return getValue(key, fallback); }
    int64_t getLong64(const char* key, int64_t fallback = 0) { return getValue(key, fallback); }
    uint64_t getULong64(const char* key, uint64_t fallback = 0) { return getValue(key, fallback); }
    float getFloat(const char* key, float fallback = 0) { return getValue(key, fallback); }
    bool getBool(const char* key, bool fallback = false) { return getValue<uint8_t>(key, fallback) != 0; }
    size_t getString(const char* key, char* value, size_t maxLength);
    String getString(const char* key, const String& fallback = String());
    size_t getBytesLength(const char* key);
    size_t getBytes(const char* key, void* buffer, size_t maxLength);
    size_t freeEntries() { return 256; }

private:
    size_t putValue(const char* key, const void* value, size_t length);
    template<typename T>
    T getValue(const char* key, T fallback) {
        const auto* entry = find(key);
        if (entry == nullptr || entry->size() != sizeof(T))
            return fallback;
        T value;
        std::memcpy(&value, entry->data(), sizeof(T));
        return value;
    }
    const std::vector<uint8_t>* find(const char* key) const;
    bool save();
    std::string path() const;

    std::string name_;
    bool open_ = false;
    bool readOnly_ = false;
    std::map<std::string, std::vector<uint8_t>> values_;
};
