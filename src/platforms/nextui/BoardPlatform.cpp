// Board::* interfaces for Linux handhelds running NextUI (and desktop Linux for development).
#include "board/BoardAudio.h"
#include "board/BoardConfig.h"
#include "board/BoardDisplay.h"
#include "board/BoardImu.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardStorage.h"
#include "board/BoardSystem.h"

#include <FS.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/statvfs.h>

#include "platforms/nextui/NextUiDisplay.h"
#include "platforms/nextui/NextUiHost.h"

// -------------------------------------------------------------------------------------------------
// Display: an RGB565 canvas the host presents (scaled) on every flush.
// -------------------------------------------------------------------------------------------------
namespace Board::Display {

    bool begin() {
        return NextUiDisplay::canvas().begin(GFX_SKIP_OUTPUT_BEGIN);
    }
    Arduino_GFX& gfx() {
        return NextUiDisplay::canvas();
    }
    ui::Orientation defaultUiOrientation() {
        return ui::Orientation::Portrait; // Rotation 0: the panel is already landscape.
    }
    ui::Orientation rotatedUiOrientation() {
        return ui::Orientation::PortraitFlipped;
    }
    uint16_t nativeWidth() {
        return NextUiHost::kPanelWidth;
    }
    uint16_t nativeHeight() {
        return NextUiHost::kPanelHeight;
    }
    size_t txChunkBytes() {
        return static_cast<size_t>(NextUiHost::kPanelWidth) * NextUiHost::kPanelHeight * 2;
    }
    void setBacklight(bool on) {
        NextUiHost::setBacklight(on);
    }
    void setBrightness(uint8_t percent) {
        NextUiHost::setBrightness(percent);
    }
    void sleep() {
        NextUiHost::setBacklight(false);
    }
    void wake() {
        NextUiHost::setBacklight(true);
    }
    bool pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* data) {
        auto& canvas = NextUiDisplay::canvas();
        canvas.draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(data), width, height);
        canvas.flush();
        return true;
    }

} // namespace Board::Display

// -------------------------------------------------------------------------------------------------
// Input: physical buttons map onto firmware actions; the D-pad/A/shoulders drive a virtual touch.
// -------------------------------------------------------------------------------------------------
namespace Board::Input {

    bool begin() {
        return true;
    }
    void end() {}
    void cancel() {}

    ::Input::ControlTiming controlTiming() {
        return {.debounceMs = 10, .shortPressMaxMs = 700, .longPressMs = 900};
    }

    ::Input::PressActions currentActions() {
        using namespace NextUiHost;
        const uint32_t held = buttonsHeld();
        ::Input::PressActions actions = {};
        if (held & ButtonX)
            actions.shortPress |= ::Input::ActionSelect | ::Input::ActionPlayPause;
        if (held & ButtonB)
            actions.shortPress |= ::Input::ActionBack;
        // Start is the Brick's play/pause key; the TrimUI (Menu) key opens the menu.
        if (held & ButtonStart)
            actions.shortPress |= ::Input::ActionPlayPause;
        if (held & ButtonMenu) {
            actions.shortPress |= ::Input::ActionOpenMenu;
            actions.longPress |= ::Input::ActionPowerOff;
        }
        if (held & ButtonSelect)
            actions.longPress |= ::Input::ActionStandby;
        if (held & ButtonPower) {
            actions.shortPress |= ::Input::ActionStandby;
            actions.longPress |= ::Input::ActionPowerOff;
        }
        return actions;
    }

    ui::TouchSurface touchSurface() {
        return {NextUiHost::kPanelWidth, NextUiHost::kPanelHeight};
    }

    ::Input::TouchTiming touchTiming() {
        return {
            .releaseConfirmSamples = 1,
            .readyPollIntervalMs = 8,
            .pollIntervalMs = 8,
        };
    }

    bool beginTouch() {
        return true;
    }

    bool touchReady() {
        ui::TouchContact contact;
        return NextUiHost::touchContact(contact) && contact.touched;
    }

    bool readTouch(ui::TouchContact& contact) {
        NextUiHost::touchContact(contact);
        return true;
    }

} // namespace Board::Input

// -------------------------------------------------------------------------------------------------
// Storage: the "SD card" is a directory on the host (by default the pak's data folder).
// -------------------------------------------------------------------------------------------------
namespace Board::Storage {

    namespace {
        fs::FS& hostFilesystem() {
            static fs::FS filesystem;
            return filesystem;
        }
    } // namespace

    fs::FS& filesystem() {
        return hostFilesystem();
    }

    bool mount(const char*, int) {
        const std::string& root = NextUiHost::dataRoot();
        std::error_code error;
        std::filesystem::create_directories(root, error);
        if (!std::filesystem::is_directory(root, error))
            return false;
        hostFilesystem().setRoot(root);
        return true;
    }

    void end() {}

    uint64_t cardSize() {
        struct statvfs info{};
        if (statvfs(NextUiHost::dataRoot().c_str(), &info) != 0)
            return 0;
        return static_cast<uint64_t>(info.f_blocks) * info.f_frsize;
    }

    CardType cardType() {
        return CardType::Sdhc;
    }

    bool supportsFrequencySelection() {
        return false;
    }

    bool setSdMmcPins() {
        return true;
    }

} // namespace Board::Storage

// -------------------------------------------------------------------------------------------------
// Power: battery from sysfs; "power off" returns to NextUI.
// -------------------------------------------------------------------------------------------------
namespace Board::Power {

    namespace {
        std::string readLine(const std::filesystem::path& path) {
            std::ifstream file(path);
            std::string line;
            std::getline(file, line);
            return line;
        }

        std::filesystem::path batteryDirectory() {
            std::error_code error;
            for (const auto& entry: std::filesystem::directory_iterator("/sys/class/power_supply", error)) {
                if (readLine(entry.path() / "type") == "Battery")
                    return entry.path();
            }
            return {};
        }

        bool mainsOnline() {
            std::error_code error;
            for (const auto& entry: std::filesystem::directory_iterator("/sys/class/power_supply", error)) {
                const std::string type = readLine(entry.path() / "type");
                if ((type == "Mains" || type == "USB") && readLine(entry.path() / "online") == "1")
                    return true;
            }
            return false;
        }
    } // namespace

    void begin() {}

    bool enableAudioPowerIfAvailable() {
        return false;
    }

    bool readBatteryStatus(BatteryStatus& status) {
        const std::filesystem::path battery = batteryDirectory();
        if (battery.empty())
            return false;
        const std::string capacity = readLine(battery / "capacity");
        if (capacity.empty())
            return false;
        status.present = true;
        status.percent = static_cast<uint8_t>(std::clamp(std::atoi(capacity.c_str()), 0, 100));
        const std::string microvolts = readLine(battery / "voltage_now");
        status.voltage = microvolts.empty() ? 0.0f : static_cast<float>(std::atof(microvolts.c_str()) / 1'000'000.0);
        return true;
    }

    Diagnostics readDiagnostics() {
        return {.available = false, .externalPowerPresent = externalPowerPresent()};
    }

    bool externalPowerPresent() {
        const std::filesystem::path battery = batteryDirectory();
        if (!battery.empty()) {
            const std::string status = readLine(battery / "status");
            if (status == "Charging" || status == "Full")
                return true;
        }
        return mainsOnline();
    }

    bool powerOff() {
        // Progress and settings are already flushed by App::powerOff; hand the device back to NextUI.
        NextUiHost::requestQuit();
        while (true)
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    bool powerButtonHeld() {
        return (NextUiHost::buttonsHeld() & NextUiHost::ButtonPower) != 0;
    }

} // namespace Board::Power

// -------------------------------------------------------------------------------------------------
// System: "light sleep" turns the screen off until a button is pressed.
// -------------------------------------------------------------------------------------------------
namespace Board::System {

    void begin() {}

    EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
        using clock = std::chrono::steady_clock;
        NextUiHost::setBacklight(false);
        const auto start = clock::now();
        // Wait for every button to be released first, then for a new press.
        while (NextUiHost::buttonsHeld() != 0 && !NextUiHost::quitRequested())
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        EspLightSleep::WakeReason reason = EspLightSleep::WakeReason::timer;
        while (!NextUiHost::quitRequested()) {
            if (timeoutMs != 0 && clock::now() - start >= std::chrono::milliseconds(timeoutMs))
                break;
            if (NextUiHost::buttonsHeld() != 0) {
                reason = EspLightSleep::WakeReason::input;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        NextUiHost::setBacklight(true);
        return reason;
    }

    const char* wakeLabel(bool externalPowerPresent) {
        return externalPowerPresent ? "charging" : "button";
    }

    void logStartupDiagnostics() {
        ESP_LOGI("board", "%s data=%s state=%s panel=%ux%u", Board::Config::BOARD_LABEL,
                 NextUiHost::dataRoot().c_str(), NextUiHost::stateRoot().c_str(), NextUiHost::kPanelWidth,
                 NextUiHost::kPanelHeight);
    }

} // namespace Board::System

// -------------------------------------------------------------------------------------------------
// No IMU and no audio codec are exposed to the firmware on this platform.
// -------------------------------------------------------------------------------------------------
namespace Board::Imu {
    bool available() {
        return false;
    }
    const char* wireName() {
        return "none";
    }
    uint8_t address() {
        return 0;
    }
    bool probeAddress(uint8_t) {
        return false;
    }
    bool readRegister(uint8_t, uint8_t, uint8_t&) {
        return false;
    }
    bool writeRegister(uint8_t, uint8_t, uint8_t) {
        return false;
    }
    bool readRegisters(uint8_t, uint8_t, uint8_t*, size_t) {
        return false;
    }
} // namespace Board::Imu

namespace Board::Audio {
    bool begin() {
        return false;
    }
    bool beep() {
        return false;
    }
    bool available() {
        return false;
    }
} // namespace Board::Audio

// PdfConverter opens documents through the firmware's VFS mount point ("/sdcard/library/...").
const char* rsvp_host_vfs_path(const char* path) {
    thread_local std::string mapped;
    constexpr std::string_view kMountPoint = "/sdcard";
    const std::string_view view = path != nullptr ? path : "";
    if (view.starts_with(kMountPoint) && (view.size() == kMountPoint.size() || view[kMountPoint.size()] == '/')) {
        mapped = NextUiHost::dataRoot();
        mapped.append(view.substr(kMountPoint.size()));
        return mapped.c_str();
    }
    return path;
}
