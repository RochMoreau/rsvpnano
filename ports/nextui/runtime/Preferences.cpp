// Preferences (NVS) persisted as <stateRoot>/nvs/<namespace>.nvs.
#include <Preferences.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <sys/stat.h>

namespace {
    std::string gDirectory;
    std::mutex gMutex;

    // File format: repeated [u16 keyLength][key][u32 valueLength][value].
    bool readAll(FILE* file, std::map<std::string, std::vector<uint8_t>>& values) {
        while (true) {
            uint16_t keyLength = 0;
            if (std::fread(&keyLength, sizeof keyLength, 1, file) != 1)
                return true;
            std::string key(keyLength, '\0');
            uint32_t valueLength = 0;
            if (std::fread(key.data(), 1, keyLength, file) != keyLength
                || std::fread(&valueLength, sizeof valueLength, 1, file) != 1 || valueLength > (1U << 20))
                return false;
            std::vector<uint8_t> value(valueLength);
            if (std::fread(value.data(), 1, valueLength, file) != valueLength)
                return false;
            values[std::move(key)] = std::move(value);
        }
    }
} // namespace

void Preferences::setStorageDirectory(std::string directory) {
    const std::lock_guard lock{gMutex};
    gDirectory = std::move(directory);
    ::mkdir(gDirectory.c_str(), 0775);
}

std::string Preferences::path() const {
    return gDirectory + "/" + name_ + ".nvs";
}

bool Preferences::begin(const char* name, bool readOnly, const char*) {
    if (name == nullptr || *name == '\0' || std::strlen(name) > 15)
        return false;
    const std::lock_guard lock{gMutex};
    if (gDirectory.empty())
        return false;
    name_ = name;
    readOnly_ = readOnly;
    values_.clear();
    if (FILE* file = std::fopen(path().c_str(), "rb")) {
        if (!readAll(file, values_))
            values_.clear();
        std::fclose(file);
    }
    open_ = true;
    return true;
}

void Preferences::end() {
    open_ = false;
    values_.clear();
}

bool Preferences::save() {
    if (!open_ || readOnly_)
        return false;
    const std::lock_guard lock{gMutex};
    const std::string target = path();
    const std::string temp = target + ".tmp";
    FILE* file = std::fopen(temp.c_str(), "wb");
    if (file == nullptr)
        return false;
    bool ok = true;
    for (const auto& [key, value]: values_) {
        const uint16_t keyLength = static_cast<uint16_t>(key.size());
        const uint32_t valueLength = static_cast<uint32_t>(value.size());
        ok = ok && std::fwrite(&keyLength, sizeof keyLength, 1, file) == 1
          && std::fwrite(key.data(), 1, key.size(), file) == key.size()
          && std::fwrite(&valueLength, sizeof valueLength, 1, file) == 1
          && std::fwrite(value.data(), 1, value.size(), file) == value.size();
    }
    ok = std::fflush(file) == 0 && ok;
    std::fclose(file);
    return ok && std::rename(temp.c_str(), target.c_str()) == 0;
}

bool Preferences::clear() {
    if (!open_ || readOnly_)
        return false;
    values_.clear();
    return save();
}

bool Preferences::remove(const char* key) {
    if (!open_ || readOnly_ || key == nullptr)
        return false;
    values_.erase(key);
    return save();
}

bool Preferences::isKey(const char* key) {
    return find(key) != nullptr;
}

const std::vector<uint8_t>* Preferences::find(const char* key) const {
    if (!open_ || key == nullptr)
        return nullptr;
    const auto it = values_.find(key);
    return it == values_.end() ? nullptr : &it->second;
}

size_t Preferences::putValue(const char* key, const void* value, size_t length) {
    if (!open_ || readOnly_ || key == nullptr || std::strlen(key) > 15 || (value == nullptr && length != 0))
        return 0;
    const auto* bytes = static_cast<const uint8_t*>(value);
    values_[key] = std::vector<uint8_t>(bytes, bytes + length);
    return save() ? length : 0;
}

size_t Preferences::putString(const char* key, const char* value) {
    if (value == nullptr)
        return 0;
    // Stored with its terminator, like NVS strings.
    return putValue(key, value, std::strlen(value) + 1) == 0 ? 0 : std::strlen(value);
}

size_t Preferences::getString(const char* key, char* value, size_t maxLength) {
    const auto* entry = find(key);
    if (entry == nullptr || entry->empty())
        return 0;
    if (value == nullptr)
        return entry->size();
    if (entry->size() > maxLength)
        return 0;
    std::memcpy(value, entry->data(), entry->size());
    return entry->size();
}

String Preferences::getString(const char* key, const String& fallback) {
    const auto* entry = find(key);
    if (entry == nullptr || entry->empty())
        return fallback;
    return String(reinterpret_cast<const char*>(entry->data()), entry->size() - 1);
}

size_t Preferences::getBytesLength(const char* key) {
    const auto* entry = find(key);
    return entry == nullptr ? 0 : entry->size();
}

size_t Preferences::getBytes(const char* key, void* buffer, size_t maxLength) {
    const auto* entry = find(key);
    if (entry == nullptr || buffer == nullptr || entry->size() > maxLength)
        return 0;
    std::memcpy(buffer, entry->data(), entry->size());
    return entry->size();
}
