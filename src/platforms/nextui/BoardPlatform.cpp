// Board::* interfaces for Linux handhelds running NextUI (and desktop Linux for development).
#include "board/BoardAudio.h"
#include "board/BoardConfig.h"
#include "board/BoardDisplay.h"
#include "board/BoardImu.h"
#include "board/BoardIndicators.h"
#include "board/BoardInput.h"
#include "board/BoardPower.h"
#include "board/BoardStorage.h"
#include "board/BoardSystem.h"

#include <FS.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include "platforms/nextui/NextUiDisplay.h"
#include "platforms/nextui/NextUiFeedback.h"
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
        // Shoulders jump in the reader (menus ignore these and get swipes from the navigator instead).
        if (held & ButtonL1)
            actions.shortPress |= ::Input::ActionPreviousSentence;
        if (held & ButtonR1)
            actions.shortPress |= ::Input::ActionNextSentence;
        if (held & ButtonL2)
            actions.shortPress |= ::Input::ActionPreviousParagraph;
        if (held & ButtonR2)
            actions.shortPress |= ::Input::ActionNextParagraph;
        if (held & ButtonMenu) {
            actions.shortPress |= ::Input::ActionOpenMenu;
            actions.longPress |= ::Input::ActionPowerOff;
        }
        if (held & ButtonSelect)
            actions.longPress |= ::Input::ActionStandby;
        if (held & ButtonPower) {
            actions.shortPress |= ::Input::ActionSleep; // screen off and suspend, like NextUI
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

    // Sleep like NextUI does: backlight off at once ("hybrid" sleep), then after NextUI's suspend
    // timeout the system suspends (NextUI's own suspend script) until the power key is pressed.
    // While charging the Brick stays in hybrid sleep, as NextUI does.
    EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
        using clock = std::chrono::steady_clock;
        using std::chrono::milliseconds;
        const int backlight = NextUiFeedback::backlight();
        NextUiFeedback::end(); // NextUI's LEDs while asleep
        NextUiFeedback::sleepPulse();
        NextUiHost::setBacklight(false);
        NextUiFeedback::setBacklight(0);
        ESP_LOGI("sleep", "screen off (backlight was %d)", backlight);

        const auto start = clock::now();
        const uint32_t suspendAfterSecs = NextUiFeedback::suspendTimeoutSecs();
        auto suspendAt = start + std::chrono::seconds(suspendAfterSecs);
        const auto powerHeld = [] {
            return (NextUiHost::buttonsHeld() & NextUiHost::ButtonPower) != 0;
        };
        // The press that put us to sleep must be released before a new one can wake us.
        while (powerHeld() && !NextUiHost::quitRequested())
            std::this_thread::sleep_for(milliseconds(20));

        EspLightSleep::WakeReason reason = EspLightSleep::WakeReason::input;
        while (!NextUiHost::quitRequested() && !powerHeld()) {
            const auto now = clock::now();
            if (timeoutMs != 0 && now - start >= milliseconds(timeoutMs)) {
                reason = EspLightSleep::WakeReason::timer;
                break;
            }
            if (suspendAfterSecs > 0 && now >= suspendAt) {
                if (Board::Power::externalPowerPresent()) {
                    suspendAt = now + std::chrono::seconds(60); // charging: check again in a minute
                    continue;
                }
                const char* system = std::getenv("SYSTEM_PATH");
                const std::string script = std::string{system != nullptr ? system : ""} + "/bin/suspend";
                if (system != nullptr && access(script.c_str(), X_OK) == 0) {
                    ESP_LOGI("sleep", "suspending");
                    const int result = std::system(("\"" + script + "\"").c_str());
                    ESP_LOGI("sleep", "resumed (%d)", result);
                    break; // The power key woke the system.
                }
                suspendAt = clock::time_point::max(); // No suspend script: stay in hybrid sleep.
            }
            std::this_thread::sleep_for(milliseconds(50));
        }

        if (reason == EspLightSleep::WakeReason::timer)
            return reason; // The firmware powers off (returns to NextUI) next.
        NextUiFeedback::setBacklight(8); // NextUI wakes the Brick's panel at a low level first.
        NextUiFeedback::setBacklight(backlight > 0 ? backlight : 128);
        NextUiHost::setBacklight(true);
        NextUiHost::afterWake();
        NextUiFeedback::begin();
        NextUiFeedback::sleepPulse();
        ESP_LOGI("sleep", "awake");
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
// No IMU on this platform. Audio and status lights go through NextUiFeedback.
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
        return true;
    }
    // The firmware beeps when a focus or break phase ends: chime, rumble and LEDs on the Brick.
    bool beep() {
        NextUiFeedback::alarm();
        return true;
    }
    bool available() {
        return true;
    }
} // namespace Board::Audio

namespace Board::Indicators {
    void show(const State& state) {
        NextUiFeedback::showIndicators(state);
    }
} // namespace Board::Indicators

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
