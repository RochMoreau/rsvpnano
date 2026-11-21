// Firmware services with no NextUI equivalent. Each keeps the firmware's interface and reports
// itself unavailable so the existing UI paths show a sensible status instead of failing.
#include <Arduino.h>

#include "companion/http/CompanionApi.h"
#include "companion/serial/CompanionSerial.h"
#include "feeds/RssFeeds.h"
#include "logging/Logger.h"
#include "network/WifiConnection.h"
#include "settings/NvsSecurity.h"
#include "update/OtaUpdater.h"
#include "usb/UsbMassStorageManager.h"

#include <FirmwareVersion.generated.h>

// --- USB mass storage: the Brick's SD card is reached by removing it or over the network. ---
UsbMassStorageManager* UsbMassStorageManager::instance_ = nullptr;

UsbMassStorageManager::UsbMassStorageManager() = default;

bool UsbMassStorageManager::begin(bool) {
    statusMessage_ = "USB transfer is not available on this device";
    return false;
}

void UsbMassStorageManager::end() {
    active_ = false;
}

bool UsbMassStorageManager::active() const {
    return false;
}

bool UsbMassStorageManager::ejected() const {
    return false;
}

uint64_t UsbMassStorageManager::cardSizeBytes() const {
    return 0;
}

const char* UsbMassStorageManager::statusMessage() const {
    return statusMessage_;
}

// --- Companion Wi-Fi server and serial protocol. ---
bool CompanionApi::begin() {
    statusLine1_ = "Wireless sync is not available";
    statusLine2_ = "Copy books to the SD card";
    return false;
}

void CompanionApi::end() {
    active_ = false;
}

bool CompanionApi::active() const {
    return false;
}

void CompanionApi::renderStatus(bool) {}

std::string_view CompanionApi::statusLine1() const {
    return statusLine1_;
}

std::string_view CompanionApi::statusLine2() const {
    return statusLine2_;
}

void* httpd_get_global_user_ctx(httpd_handle_t) {
    return nullptr;
}

esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) {
    return ESP_ERR_NOT_SUPPORTED;
}

void CompanionSerial::update(uint32_t) {}

void CompanionSerial::close() {
    active_ = false;
}

bool CompanionSerial::active() const noexcept {
    return false;
}

// --- Network: Wi-Fi belongs to NextUI. ---
namespace net {
    std::expected<void, std::error_code> connectStation(const char*, const char*, const WifiProgress&, uint32_t) {
        return std::unexpected(std::make_error_code(std::errc::operation_not_supported));
    }
    void disconnect() {}
} // namespace net

namespace RssFeeds {
    Result check(Preferences&, const settings::DeviceSettings&, const settings::DeviceSecrets&, StatusCallback,
                 void*) {
        Result result;
        result.summary = "Feeds unavailable";
        result.detail = "RSS sync is not supported on NextUI yet";
        return result;
    }
} // namespace RssFeeds

// --- Updates ship as a new pak, not over the air. ---
namespace OtaUpdater {
    std::string_view currentVersion() {
        return kFirmwareVersion;
    }
    Result checkOnly(const settings::DeviceSettings&, const settings::DeviceSecrets&, StatusCallback, void*) {
        return {.summary = "Updates via pak", .detail = "Install a newer RSVP Nano pak on the SD card"};
    }
    Result checkAndInstall(const settings::DeviceSettings& settings, const settings::DeviceSecrets& secrets,
                           StatusCallback callback, void* context) {
        return checkOnly(settings, secrets, callback, context);
    }
} // namespace OtaUpdater

// --- NVS encryption: host preferences are plain files under the pak's state directory. ---
namespace settings {
    NvsEncryptionState nvsEncryptionState() {
        return NvsEncryptionState::Unavailable;
    }
    bool initializeNvsEncryption() {
        return true;
    }
    bool enableNvsEncryption(Preferences&, SettingsStore&) {
        return false;
    }
} // namespace settings

// --- Logger: no retained crash records; checkpoints go to the log. ---
namespace Logger {
    void begin() {}
    void logResetReason() {}
    void checkpoint(const char* phase) {
        ESP_LOGI("checkpoint", "%s", phase != nullptr ? phase : "");
    }
    void startupCheckpoint(const char* phase) {
        checkpoint(phase);
    }
} // namespace Logger
