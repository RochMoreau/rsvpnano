#include "platforms/nextui/NextUiFeedback.h"

#include <SDL.h>
#include <esp_log.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace NextUiFeedback {
    namespace {

        constexpr const char* kTag = "feedback";

        bool enabled(const char* variable) {
            const char* value = std::getenv(variable);
            return value == nullptr || std::string_view{value} != "0";
        }

        std::string devicePath(std::string_view path) {
            const char* root = std::getenv("RSVP_SYSFS_ROOT");
            return std::string{root != nullptr ? root : ""} + std::string{path};
        }

        bool writeText(std::string_view path, const std::string& text) {
            std::ofstream file(devicePath(path));
            if (!file)
                return false;
            file << text << '\n';
            return static_cast<bool>(file);
        }

        std::string sharedUserdata() {
            const char* path = std::getenv("SHARED_USERDATA_PATH");
            return path != nullptr ? path : "/mnt/SDCARD/.userdata/shared";
        }

        // Integer setting from NextUI's minuisettings.txt ("key=value" lines).
        std::optional<int> nextUiSetting(std::string_view key) {
            std::ifstream file(sharedUserdata() + "/minuisettings.txt");
            std::string line;
            while (std::getline(file, line)) {
                if (line.size() > key.size() && line.compare(0, key.size(), key) == 0 && line[key.size()] == '=')
                    return std::atoi(line.c_str() + key.size() + 1);
            }
            return std::nullopt;
        }

        // ---------------------------------------------------------------------------------------
        // Rumble: /sys/class/motor/voltage sets the strength, gpio227 switches the motor.
        // ---------------------------------------------------------------------------------------
        constexpr int kRumbleMicrovolts = 2500000; // NextUI's cap on the Brick Pro; firm, not harsh.
        std::atomic<bool> gRumbling{false};

        void rumble(std::vector<std::pair<int, int>> pattern) { // (on ms, off ms) pairs
            if (!enabled("RSVP_RUMBLE") || gRumbling.exchange(true))
                return;
            std::thread([pattern = std::move(pattern)] {
                writeText("/sys/class/motor/voltage", std::to_string(kRumbleMicrovolts));
                for (const auto& [onMs, offMs]: pattern) {
                    writeText("/sys/class/gpio/gpio227/value", "1");
                    std::this_thread::sleep_for(std::chrono::milliseconds(onMs));
                    writeText("/sys/class/gpio/gpio227/value", "0");
                    std::this_thread::sleep_for(std::chrono::milliseconds(offMs));
                }
                gRumbling = false;
            }).detach();
        }

        // ---------------------------------------------------------------------------------------
        // Chime through SDL audio (the system volume applies). The device is opened per chime and
        // closed afterwards, so nothing is left open across a suspend.
        // ---------------------------------------------------------------------------------------
        std::atomic<bool> gChiming{false};

        std::vector<int16_t> chimeSamples(int rate) {
            struct Note {
                double hz;
                double startS;
                double lengthS;
            };
            // Two rising notes (E6, A6) with a soft attack and exponential decay.
            constexpr Note notes[] = {{1318.5, 0.0, 0.45}, {1760.0, 0.16, 0.6}};
            const size_t total = static_cast<size_t>(rate * 0.8);
            std::vector<int16_t> samples(total);
            for (size_t i = 0; i < total; ++i) {
                const double t = static_cast<double>(i) / rate;
                double value = 0;
                for (const Note& note: notes) {
                    const double local = t - note.startS;
                    if (local < 0 || local > note.lengthS)
                        continue;
                    const double attack = std::min(1.0, local / 0.006);
                    const double envelope = attack * std::exp(-local * 7.0);
                    value += 0.3 * envelope * (std::sin(2 * M_PI * note.hz * local)
                                               + 0.25 * std::sin(4 * M_PI * note.hz * local));
                }
                samples[i] = static_cast<int16_t>(std::clamp(value, -1.0, 1.0) * 32000);
            }
            return samples;
        }

        void chime() {
            if (!enabled("RSVP_SOUND") || gChiming.exchange(true))
                return;
            std::thread([] {
                if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
                    ESP_LOGW(kTag, "audio unavailable: %s", SDL_GetError());
                    gChiming = false;
                    return;
                }
                SDL_AudioSpec wanted{};
                wanted.freq = 44100;
                wanted.format = AUDIO_S16SYS;
                wanted.channels = 1;
                wanted.samples = 1024;
                SDL_AudioSpec obtained{};
                const SDL_AudioDeviceID device = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
                if (device == 0) {
                    ESP_LOGW(kTag, "audio open failed: %s", SDL_GetError());
                } else {
                    const std::vector<int16_t> samples = chimeSamples(obtained.freq);
                    SDL_QueueAudio(device, samples.data(), static_cast<Uint32>(samples.size() * sizeof(int16_t)));
                    SDL_PauseAudioDevice(device, 0);
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                    while (SDL_GetQueuedAudioSize(device) > 0 && std::chrono::steady_clock::now() < deadline)
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    SDL_CloseAudioDevice(device);
                }
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
                gChiming = false;
            }).detach();
        }

        // ---------------------------------------------------------------------------------------
        // LEDs: /sys/class/led_anim/{effect,effect_rgb_hex,effect_duration,effect_cycles}_<led>.
        // Writing the effect applies the others. Brightness stays NextUI's setting.
        // ---------------------------------------------------------------------------------------
        constexpr int kEffectBreathe = 2;
        constexpr int kEffectBlink = 3;
        constexpr int kEffectStatic = 4;
        constexpr const char* kStatusLed = "m"; // Brick top bar / Smart Pro logo

        struct Light {
            std::string name;
            int effect = kEffectStatic;
            uint32_t color = 0xFFFFFF;
            int durationMs = 1000;
            int cycles = -1;

            bool operator==(const Light&) const = default;
        };

        std::mutex gLedMutex;
        std::vector<Light> gNextUiLights; // what NextUI shows; restored when we are done
        std::optional<Light> gStatusShown; // what we last put on the status LED
        Board::Indicators::State gState;
        std::chrono::steady_clock::time_point gAlarmUntil{};
        bool gLedsActive = false;

        void apply(const Light& light) {
            const std::string suffix = "_" + light.name;
            writeText("/sys/class/led_anim/effect_cycles" + suffix, std::to_string(light.cycles));
            writeText("/sys/class/led_anim/effect_duration" + suffix, std::to_string(light.durationMs));
            char color[8];
            std::snprintf(color, sizeof(color), "%06X", static_cast<unsigned>(light.color & 0xFFFFFF));
            writeText("/sys/class/led_anim/effect_rgb_hex" + suffix, color);
            writeText("/sys/class/led_anim/effect" + suffix, std::to_string(light.effect));
        }

        // The LED setup NextUI applies, from LedControl's settings file, or NextUI's defaults.
        std::vector<Light> loadNextUiLights() {
            const bool brick = access(devicePath("/sys/class/led_anim/effect_f1").c_str(), F_OK) == 0;
            static constexpr std::array<const char*, 4> kBrickLights{"f1", "f2", "m", "lr"};
            static constexpr std::array<const char*, 3> kSmartProLights{"l", "r", "m"};
            std::vector<Light> lights;
            if (brick) {
                for (const char* name: kBrickLights)
                    lights.push_back({.name = name});
            } else {
                for (const char* name: kSmartProLights)
                    lights.push_back({.name = name});
            }
            std::ifstream file(sharedUserdata() + (brick ? "/ledsettings_brick.txt" : "/ledsettings.txt"));
            std::string line;
            int section = -1;
            while (std::getline(file, line)) {
                if (!line.empty() && line[0] == '[') {
                    ++section;
                    continue;
                }
                if (section < 0 || section >= static_cast<int>(lights.size()))
                    continue;
                Light& light = lights[static_cast<size_t>(section)];
                char text[64];
                unsigned value = 0;
                int number = 0;
                if (std::sscanf(line.c_str(), "filename=%63s", text) == 1)
                    light.name = text;
                else if (std::sscanf(line.c_str(), "effect=%d", &number) == 1)
                    light.effect = number;
                else if (std::sscanf(line.c_str(), "color1=%x", &value) == 1)
                    light.color = value;
                else if (std::sscanf(line.c_str(), "speed=%d", &number) == 1)
                    light.durationMs = number;
            }
            return lights;
        }

        const Light* nextUiLight(std::string_view name) {
            for (const Light& light: gNextUiLights) {
                if (light.name == name)
                    return &light;
            }
            return nullptr;
        }

        uint32_t blend(uint32_t from, uint32_t to, uint16_t permille) {
            uint32_t result = 0;
            for (int shift = 0; shift <= 16; shift += 8) {
                const int a = static_cast<int>((from >> shift) & 0xFF);
                const int b = static_cast<int>((to >> shift) & 0xFF);
                result |= static_cast<uint32_t>(a + (b - a) * std::min<int>(permille, 1000) / 1000) << shift;
            }
            return result;
        }

        // What the status LED should show for a reader state (nullopt: NextUI's own setup).
        std::optional<Light> statusLight(const Board::Indicators::State& state) {
            using Board::Indicators::Activity;
            constexpr uint32_t kAccent = 0xE62828; // the reader's anchor-letter red
            constexpr uint32_t kMiddle = 0xF0A020;
            constexpr uint32_t kDone = 0x30D060;
            constexpr uint32_t kBreak = 0x20B0E0;
            switch (state.activity) {
            case Activity::Reading: {
                // Red at the start of the book, amber half way, green at the end; 2% steps.
                const uint16_t step = static_cast<uint16_t>(state.progressPermille / 20 * 20);
                const uint32_t color = step < 500 ? blend(kAccent, kMiddle, static_cast<uint16_t>(step * 2))
                                                  : blend(kMiddle, kDone, static_cast<uint16_t>((step - 500) * 2));
                return Light{.name = kStatusLed, .effect = kEffectStatic, .color = color};
            }
            case Activity::Focus:
                return Light{.name = kStatusLed, .effect = kEffectBreathe, .color = kAccent, .durationMs = 4000};
            case Activity::Break:
                return Light{.name = kStatusLed, .effect = kEffectBreathe, .color = kBreak, .durationMs = 4000};
            case Activity::FocusPaused:
                return Light{.name = kStatusLed, .effect = kEffectStatic, .color = kAccent};
            case Activity::Idle:
                break;
            }
            return std::nullopt;
        }

        // Puts the status LED in the state for gState. Caller holds gLedMutex.
        void refreshStatusLocked() {
            if (!gLedsActive || std::chrono::steady_clock::now() < gAlarmUntil)
                return;
            std::optional<Light> wanted = statusLight(gState);
            if (wanted == gStatusShown)
                return;
            if (wanted) {
                apply(*wanted);
            } else if (const Light* original = nextUiLight(kStatusLed)) {
                apply(*original);
            }
            gStatusShown = wanted;
        }

    } // namespace

    void begin() {
        const std::lock_guard lock{gLedMutex};
        gLedsActive = enabled("RSVP_LEDS");
        gNextUiLights = loadNextUiLights();
        gStatusShown.reset();
    }

    void end() {
        const std::lock_guard lock{gLedMutex};
        if (gLedsActive) {
            for (const Light& light: gNextUiLights)
                apply(light);
        }
        gStatusShown.reset();
        gState = {};
        gAlarmUntil = {};
    }

    void alarm() {
        ESP_LOGI(kTag, "alarm");
        chime();
        rumble({{180, 120}, {180, 120}, {260, 0}});
        {
            const std::lock_guard lock{gLedMutex};
            if (!gLedsActive)
                return;
            for (const Light& light: gNextUiLights)
                apply({.name = light.name, .effect = kEffectBlink, .color = 0xFFFFFF, .durationMs = 500, .cycles = 6});
            gAlarmUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(3200);
            gStatusShown.reset();
        }
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::milliseconds(3300));
            const std::lock_guard lock{gLedMutex};
            if (!gLedsActive)
                return;
            for (const Light& light: gNextUiLights) {
                if (light.name != kStatusLed)
                    apply(light);
            }
            gStatusShown = Light{.name = "", .effect = -1}; // differs from anything: forces a rewrite
            refreshStatusLocked();
        }).detach();
    }

    void sleepPulse() {
        if (nextUiSetting("haptics").value_or(0) != 0)
            rumble({{60, 0}});
    }

    void showIndicators(const Board::Indicators::State& state) {
        const std::lock_guard lock{gLedMutex};
        gState = state;
        refreshStatusLocked();
    }

    int backlight() {
        constexpr unsigned long kGetBrightness = 0x103;
        const int fd = open(devicePath("/dev/disp").c_str(), O_RDWR);
        if (fd < 0)
            return -1;
        unsigned long parameters[4] = {0, 0, 0, 0};
        const int level = ioctl(fd, kGetBrightness, parameters);
        close(fd);
        return level;
    }

    void setBacklight(int level) {
        constexpr unsigned long kSetBrightness = 0x102;
        const int fd = open(devicePath("/dev/disp").c_str(), O_RDWR);
        if (fd < 0)
            return;
        unsigned long parameters[4] = {0, static_cast<unsigned long>(std::clamp(level, 0, 255)), 0, 0};
        ioctl(fd, kSetBrightness, parameters);
        close(fd);
    }

    uint32_t suspendTimeoutSecs() {
        return static_cast<uint32_t>(std::max(0, nextUiSetting("suspendTimeout").value_or(30)));
    }

} // namespace NextUiFeedback
