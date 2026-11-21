// SDL2 host for RSVP Nano on NextUI (TrimUI Brick) and desktop Linux.
//
// Threads:
//   main  - owns SDL: window, controller/keyboard/mouse events, presentation, focus navigation.
//   app   - runs the firmware's setup()/loop() exactly as on the ESP32.
// The two meet through NextUiHost (buttons, synthesized touch) and the RGB565 canvas.
#include "platforms/nextui/NextUiHost.h"

#include <SDL.h>

#include <Arduino.h>
#include <Preferences.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "platforms/nextui/NextUiDisplay.h"
#include "platforms/nextui/NextUiNavigator.h"
#include <RsvpRuntime.h>
#include "ui/Ui.h"

void setup();
void loop();

namespace {

    using namespace NextUiHost;

    struct Options {
        std::string dataRoot;
        std::string stateRoot;
        std::string logFile;
        std::string script;
        int scale = 2;
        bool fullscreen = false;
        int logLevel = 3;
    };

    Options gOptions;
    std::vector<std::string> gArguments;

    std::atomic<uint32_t> gDeviceButtons{0}; // Controller + keyboard, updated by the SDL thread.
    std::atomic<uint32_t> gScriptButtons{0}; // Test script.
    std::atomic<uint32_t> gInjectedButtons{0}; // Host requests (e.g. power off on window close).
    std::atomic<bool> gQuit{false};
    std::atomic<bool> gRestart{false};
    std::atomic<bool> gBacklight{true};

    std::mutex gTouchMutex;
    ui::TouchContact gTouch{};

    std::mutex gTargetsMutex;
    std::vector<ui::Context::Target> gTargets;
    uint32_t gTargetsSerial = 0;
    uint8_t gTargetsScreen = 0xff;

    std::mutex gScreenshotMutex;
    std::deque<std::string> gScreenshots;

    // ---------------------------------------------------------------------------------------------
    // Options
    // ---------------------------------------------------------------------------------------------
    std::string envOr(const char* name, const std::string& fallback) {
        const char* value = std::getenv(name);
        return value != nullptr && *value != '\0' ? std::string(value) : fallback;
    }

    void usage(const char* program) {
        std::fprintf(stderr,
                     "usage: %s [--data DIR] [--state DIR] [--log FILE] [--fullscreen] [--scale N]\n"
                     "          [--log-level 0-5] [--script COMMANDS]\n"
                     "  --data   folder used as the reader's SD card (library/, config/, themes/...)\n"
                     "  --state  folder for preferences and other device state (default: DATA/.state)\n"
                     "  --script semicolon-separated test commands: wait MS; press BTN; hold BTN MS;\n"
                     "           down BTN; up BTN; tap X Y; shot FILE.bmp; quit\n",
                     program);
    }

    bool parseOptions(int argc, char** argv) {
        const std::string sdcard = envOr("SDCARD_PATH", "");
        gOptions.dataRoot = envOr("RSVP_DATA_DIR", sdcard.empty() ? "./sdcard" : sdcard + "/RSVP Nano");
        gOptions.stateRoot = envOr("RSVP_STATE_DIR", "");
        gOptions.fullscreen = std::getenv("DEVICE") != nullptr;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto value = [&](std::string& out) {
                if (i + 1 >= argc)
                    return false;
                out = argv[++i];
                return true;
            };
            std::string number;
            if (arg == "--data") {
                if (!value(gOptions.dataRoot))
                    return false;
            } else if (arg == "--state") {
                if (!value(gOptions.stateRoot))
                    return false;
            } else if (arg == "--log") {
                if (!value(gOptions.logFile))
                    return false;
            } else if (arg == "--script") {
                if (!value(gOptions.script))
                    return false;
            } else if (arg == "--scale") {
                if (!value(number))
                    return false;
                gOptions.scale = std::clamp(std::atoi(number.c_str()), 1, 8);
            } else if (arg == "--log-level") {
                if (!value(number))
                    return false;
                gOptions.logLevel = std::clamp(std::atoi(number.c_str()), 0, 5);
            } else if (arg == "--fullscreen") {
                gOptions.fullscreen = true;
            } else if (arg == "--windowed") {
                gOptions.fullscreen = false;
            } else {
                return false;
            }
        }
        std::error_code error;
        gOptions.dataRoot = std::filesystem::absolute(gOptions.dataRoot, error).lexically_normal().string();
        while (gOptions.dataRoot.size() > 1 && gOptions.dataRoot.back() == '/')
            gOptions.dataRoot.pop_back();
        if (gOptions.stateRoot.empty())
            gOptions.stateRoot = gOptions.dataRoot + "/.state";
        gOptions.stateRoot = std::filesystem::absolute(gOptions.stateRoot, error).lexically_normal().string();
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // Controls
    // ---------------------------------------------------------------------------------------------
    // TrimUI Brick / Smart Pro joystick layout (NextUI workspace/tg5040/platform/platform.h).
    uint32_t brickJoystickButton(uint8_t index) {
        switch (index) {
        case 0:
            return ButtonB;
        case 1:
            return ButtonA;
        case 2:
            return ButtonY;
        case 3:
            return ButtonX;
        case 4:
            return ButtonL1;
        case 5:
            return ButtonR1;
        case 6:
            return ButtonSelect;
        case 7:
            return ButtonStart;
        case 8:
            return ButtonMenu;
        default:
            return 0;
        }
    }

    uint32_t gameControllerButton(uint8_t button) {
        switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            return ButtonA;
        case SDL_CONTROLLER_BUTTON_B:
            return ButtonB;
        case SDL_CONTROLLER_BUTTON_X:
            return ButtonX;
        case SDL_CONTROLLER_BUTTON_Y:
            return ButtonY;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return ButtonL1;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return ButtonR1;
        case SDL_CONTROLLER_BUTTON_BACK:
            return ButtonSelect;
        case SDL_CONTROLLER_BUTTON_START:
            return ButtonStart;
        case SDL_CONTROLLER_BUTTON_GUIDE:
            return ButtonMenu;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return ButtonUp;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return ButtonDown;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return ButtonLeft;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return ButtonRight;
        default:
            return 0;
        }
    }

    uint32_t keyboardButton(SDL_Scancode scancode) {
        switch (scancode) {
        case SDL_SCANCODE_UP:
            return ButtonUp;
        case SDL_SCANCODE_DOWN:
            return ButtonDown;
        case SDL_SCANCODE_LEFT:
            return ButtonLeft;
        case SDL_SCANCODE_RIGHT:
            return ButtonRight;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_Z:
            return ButtonA;
        case SDL_SCANCODE_ESCAPE:
        case SDL_SCANCODE_BACKSPACE:
        case SDL_SCANCODE_X:
            return ButtonB;
        case SDL_SCANCODE_SPACE:
        case SDL_SCANCODE_C:
            return ButtonX;
        case SDL_SCANCODE_V:
            return ButtonY;
        case SDL_SCANCODE_Q:
            return ButtonL1;
        case SDL_SCANCODE_W:
            return ButtonR1;
        case SDL_SCANCODE_1:
            return ButtonL2;
        case SDL_SCANCODE_2:
            return ButtonR2;
        case SDL_SCANCODE_TAB:
            return ButtonStart;
        case SDL_SCANCODE_S:
            return ButtonSelect;
        case SDL_SCANCODE_M:
            return ButtonMenu;
        case SDL_SCANCODE_POWER: // TrimUI power key arrives as scancode 102.
        case SDL_SCANCODE_P:
            return ButtonPower;
        default:
            return 0;
        }
    }

    uint32_t hatButtons(uint8_t hat) {
        uint32_t buttons = 0;
        if (hat & SDL_HAT_UP)
            buttons |= ButtonUp;
        if (hat & SDL_HAT_DOWN)
            buttons |= ButtonDown;
        if (hat & SDL_HAT_LEFT)
            buttons |= ButtonLeft;
        if (hat & SDL_HAT_RIGHT)
            buttons |= ButtonRight;
        return buttons;
    }

    uint32_t parseButton(const std::string& name) {
        static const std::pair<const char*, uint32_t> names[] = {
            {"up", ButtonUp},     {"down", ButtonDown},   {"left", ButtonLeft}, {"right", ButtonRight},
            {"a", ButtonA},       {"b", ButtonB},         {"x", ButtonX},       {"y", ButtonY},
            {"l1", ButtonL1},     {"r1", ButtonR1},       {"l2", ButtonL2},     {"r2", ButtonR2},
            {"select", ButtonSelect}, {"start", ButtonStart}, {"menu", ButtonMenu}, {"power", ButtonPower},
        };
        for (const auto& [label, button]: names)
            if (name == label)
                return button;
        return 0;
    }

    // ---------------------------------------------------------------------------------------------
    // Test script (runs on its own thread)
    // ---------------------------------------------------------------------------------------------
    std::atomic<bool> gScriptTouch{false};
    std::atomic<int> gScriptTouchX{0};
    std::atomic<int> gScriptTouchY{0};

    void runScript(std::string script) {
        std::replace(script.begin(), script.end(), ';', '\n');
        std::istringstream lines(script);
        std::string line;
        auto sleepMs = [](int ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::max(0, ms)));
        };
        while (std::getline(lines, line) && !gQuit.load()) {
            std::istringstream words(line);
            std::string command;
            if (!(words >> command))
                continue;
            if (command == "wait") {
                int ms = 0;
                words >> ms;
                sleepMs(ms);
            } else if (command == "press" || command == "hold") {
                std::string name;
                int ms = command == "press" ? 90 : 1000;
                words >> name >> ms;
                const uint32_t button = parseButton(name);
                gScriptButtons.fetch_or(button);
                sleepMs(ms);
                gScriptButtons.fetch_and(~button);
                sleepMs(160);
            } else if (command == "down" || command == "up") {
                std::string name;
                words >> name;
                const uint32_t button = parseButton(name);
                if (command == "down")
                    gScriptButtons.fetch_or(button);
                else
                    gScriptButtons.fetch_and(~button);
            } else if (command == "tap") {
                int x = 0, y = 0;
                words >> x >> y;
                gScriptTouchX = x;
                gScriptTouchY = y;
                gScriptTouch = true;
                sleepMs(80);
                gScriptTouch = false;
                sleepMs(160);
            } else if (command == "shot") {
                std::string path;
                std::getline(words >> std::ws, path);
                {
                    const std::lock_guard lock{gScreenshotMutex};
                    gScreenshots.push_back(path);
                }
                while (true) {
                    sleepMs(20);
                    const std::lock_guard lock{gScreenshotMutex};
                    if (gScreenshots.empty())
                        break;
                }
            } else if (command == "quit") {
                gQuit = true;
            } else {
                ESP_LOGW("script", "unknown command: %s", line.c_str());
            }
        }
    }

    // ---------------------------------------------------------------------------------------------
    // App thread
    // ---------------------------------------------------------------------------------------------
    void appThread() {
        setup();
        while (!gQuit.load(std::memory_order_relaxed)) {
            loop();
            // The ESP32 loop spins; on a battery-powered Linux handheld leave the core some rest.
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    void observeFrame(const ui::Context& context) {
        std::vector<ui::Context::Target> targets;
        targets.reserve(32);
        context.forEachTarget([&](const ui::Context::Target& target) {
            targets.push_back(target);
        });
        const std::lock_guard lock{gTargetsMutex};
        const bool same = context.screen() == gTargetsScreen && targets.size() == gTargets.size()
                       && std::equal(targets.begin(), targets.end(), gTargets.begin(), [](const auto& a, const auto& b) {
                              return a.rect == b.rect && a.role == b.role && a.valueX == b.valueX;
                          });
        if (same)
            return;
        gTargets = std::move(targets);
        gTargetsScreen = context.screen();
        ++gTargetsSerial;
    }

    void handleSignal(int) {
        gInjectedButtons.fetch_or(ButtonMenu); // Long Menu = save and power off.
    }

    // ---------------------------------------------------------------------------------------------
    // Presentation
    // ---------------------------------------------------------------------------------------------
    struct Presenter {
        SDL_Window* window = nullptr;
        SDL_Renderer* renderer = nullptr;
        SDL_Texture* texture = nullptr;
        SDL_Rect viewport{};
        bool integerScale_ = true;

        bool create() {
            const int width = NextUiHost::kPanelWidth * gOptions.scale;
            const int height = NextUiHost::kPanelHeight * gOptions.scale;
            Uint32 flags = SDL_WINDOW_SHOWN;
            if (gOptions.fullscreen)
                flags |= SDL_WINDOW_FULLSCREEN;
            else
                flags |= SDL_WINDOW_RESIZABLE;
            window = SDL_CreateWindow("RSVP Nano", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, flags);
            if (window == nullptr) {
                ESP_LOGE("host", "SDL_CreateWindow failed: %s", SDL_GetError());
                return false;
            }
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
            if (renderer == nullptr)
                renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
            if (renderer == nullptr) {
                ESP_LOGE("host", "SDL_CreateRenderer failed: %s", SDL_GetError());
                return false;
            }
            SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                        NextUiHost::kPanelWidth, NextUiHost::kPanelHeight);
            if (texture == nullptr) {
                ESP_LOGE("host", "SDL_CreateTexture failed: %s", SDL_GetError());
                return false;
            }
            SDL_RendererInfo info{};
            SDL_GetRendererInfo(renderer, &info);
            int outputW = 0, outputH = 0;
            SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
            ESP_LOGI("host", "video driver=%s renderer=%s output=%dx%d", SDL_GetCurrentVideoDriver(), info.name,
                     outputW, outputH);
            return true;
        }

        void layout() {
            int outputW = 0, outputH = 0;
            SDL_GetRendererOutputSize(renderer, &outputW, &outputH);
            const double fit = std::min(static_cast<double>(outputW) / NextUiHost::kPanelWidth,
                                        static_cast<double>(outputH) / NextUiHost::kPanelHeight);
            // Prefer crisp integer scaling when it fills most of the screen.
            const double scale = fit >= 1.0 && std::floor(fit) / fit > 0.85 ? std::floor(fit) : fit;
            viewport.w = static_cast<int>(NextUiHost::kPanelWidth * scale);
            viewport.h = static_cast<int>(NextUiHost::kPanelHeight * scale);
            viewport.x = (outputW - viewport.w) / 2;
            viewport.y = (outputH - viewport.h) / 2;
            const bool integer = std::floor(scale) == scale;
            if (integer != integerScale_) {
                integerScale_ = integer;
                SDL_SetTextureScaleMode(texture, integer ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
            }
        }

        SDL_Rect toScreen(const ui::Rect& rect) const {
            const double sx = static_cast<double>(viewport.w) / NextUiHost::kPanelWidth;
            const double sy = static_cast<double>(viewport.h) / NextUiHost::kPanelHeight;
            return {viewport.x + static_cast<int>(rect.x * sx), viewport.y + static_cast<int>(rect.y * sy),
                    static_cast<int>(rect.w * sx), static_cast<int>(rect.h * sy)};
        }

        bool toPanel(int x, int y, int& panelX, int& panelY) const {
            if (viewport.w <= 0 || viewport.h <= 0)
                return false;
            panelX = (x - viewport.x) * NextUiHost::kPanelWidth / viewport.w;
            panelY = (y - viewport.y) * NextUiHost::kPanelHeight / viewport.h;
            return panelX >= 0 && panelY >= 0 && panelX < NextUiHost::kPanelWidth
                && panelY < NextUiHost::kPanelHeight;
        }

        void render(const NextUiNavigator& navigator) {
            layout();
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
            if (gBacklight.load()) {
                auto& canvas = NextUiDisplay::canvas();
                if (uint16_t* pixels = canvas.getFramebuffer())
                    SDL_UpdateTexture(texture, nullptr, pixels, NextUiHost::kPanelWidth * 2);
                SDL_RenderCopy(renderer, texture, nullptr, &viewport);
                drawOverlay(navigator);
            }
            SDL_RenderPresent(renderer);
        }

        void drawOverlay(const NextUiNavigator& navigator) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            if (const auto focus = navigator.focusRect()) {
                SDL_Rect outer = toScreen(*focus);
                const int thickness = std::max(2, viewport.w / NextUiHost::kPanelWidth * 2);
                // Tint small widgets; large regions (the library shelf) only get the outline.
                if (focus->w * focus->h < NextUiHost::kPanelWidth * NextUiHost::kPanelHeight / 6) {
                    SDL_SetRenderDrawColor(renderer, 255, 196, 0, 60);
                    SDL_RenderFillRect(renderer, &outer);
                }
                SDL_SetRenderDrawColor(renderer, 255, 196, 0, 255);
                for (int i = 0; i < thickness; ++i) {
                    SDL_Rect edge{outer.x - thickness + i, outer.y - thickness + i, outer.w + 2 * (thickness - i),
                                  outer.h + 2 * (thickness - i)};
                    SDL_RenderDrawRect(renderer, &edge);
                }
            }
            if (const auto pointer = navigator.pointerVisible()) {
                const SDL_Rect dot = toScreen(ui::Rect{static_cast<int16_t>(pointer->first - 4),
                                                       static_cast<int16_t>(pointer->second - 4), 9, 9});
                SDL_SetRenderDrawColor(renderer, 255, 196, 0, 220);
                SDL_RenderFillRect(renderer, &dot);
            }
        }

        bool saveScreenshot(const std::string& path) {
            int w = 0, h = 0;
            SDL_GetRendererOutputSize(renderer, &w, &h);
            SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
            if (surface == nullptr)
                return false;
            const bool ok =
                SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) == 0
                && SDL_SaveBMP(surface, path.c_str()) == 0;
            SDL_FreeSurface(surface);
            return ok;
        }
    };

} // namespace

// -------------------------------------------------------------------------------------------------
// NextUiHost API (any thread)
// -------------------------------------------------------------------------------------------------
namespace NextUiHost {

    const std::string& dataRoot() {
        return gOptions.dataRoot;
    }

    const std::string& stateRoot() {
        return gOptions.stateRoot;
    }

    uint32_t buttonsHeld() {
        return gDeviceButtons.load() | gScriptButtons.load() | gInjectedButtons.load();
    }

    bool touchContact(ui::TouchContact& contact) {
        const std::lock_guard lock{gTouchMutex};
        contact = gTouch;
        contact.sampledAtMs = 0;
        return true;
    }

    void requestQuit() {
        gQuit = true;
    }

    bool quitRequested() {
        return gQuit.load();
    }

    void setBacklight(bool on) {
        gBacklight = on;
    }

    void setBrightness(uint8_t) {
        // Brightness belongs to NextUI's global setting (Menu + volume); the reader leaves it alone.
    }

} // namespace NextUiHost

namespace NextUiDisplay {
    HostCanvas& canvas() {
        static HostCanvas instance;
        return instance;
    }
} // namespace NextUiDisplay

namespace rsvp_runtime {
    void restartRequested() {
        gRestart = true;
        gQuit = true;
    }
} // namespace rsvp_runtime

// -------------------------------------------------------------------------------------------------
// main
// -------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
    for (int i = 0; i < argc; ++i)
        gArguments.emplace_back(argv[i]);
    if (!parseOptions(argc, argv)) {
        usage(argv[0]);
        return 2;
    }

    std::error_code error;
    std::filesystem::create_directories(gOptions.dataRoot + "/library/books", error);
    std::filesystem::create_directories(gOptions.stateRoot, error);
    rsvp_runtime::setLogLevel(gOptions.logLevel);
    if (!gOptions.logFile.empty())
        rsvp_runtime::setLogFile(gOptions.logFile.c_str());
    Preferences::setStorageDirectory(gOptions.stateRoot + "/nvs");

    std::signal(SIGTERM, handleSignal);
    std::signal(SIGINT, handleSignal);

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_ShowCursor(gOptions.fullscreen ? SDL_DISABLE : SDL_ENABLE);

    Presenter presenter;
    if (!presenter.create()) {
        SDL_Quit();
        return 1;
    }

    ui::Context::setFrameObserver(&observeFrame);
    std::thread(appThread).detach();
    if (!gOptions.script.empty())
        std::thread(runScript, gOptions.script).detach();

    NextUiNavigator navigator;
    std::vector<SDL_Joystick*> joysticks;
    std::vector<SDL_GameController*> controllers;
    uint32_t keyboardButtons = 0, joystickButtons = 0, hatState = 0, axisButtons = 0, controllerButtons = 0;
    bool mouseDown = false;
    int mouseX = 0, mouseY = 0;
    uint32_t lastFrame = UINT32_MAX;
    uint32_t lastTargetsSerial = UINT32_MAX;
    uint32_t lastRenderMs = 0;
    uint32_t quitInjectedAtMs = 0;
    bool lastBacklight = true;
    bool overlayDirty = true;

    while (!gQuit.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                if (quitInjectedAtMs == 0) {
                    gInjectedButtons.fetch_or(ButtonMenu);
                    quitInjectedAtMs = static_cast<uint32_t>(millis());
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (event.key.repeat == 0) {
                    const uint32_t button = keyboardButton(event.key.keysym.scancode);
                    keyboardButtons = event.type == SDL_KEYDOWN ? (keyboardButtons | button) : (keyboardButtons & ~button);
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (SDL_GameController* controller = SDL_GameControllerOpen(event.cdevice.which)) {
                    ESP_LOGI("host", "controller: %s", SDL_GameControllerName(controller));
                    controllers.push_back(controller);
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP: {
                const uint32_t button = gameControllerButton(event.cbutton.button);
                controllerButtons = event.type == SDL_CONTROLLERBUTTONDOWN ? (controllerButtons | button)
                                                                           : (controllerButtons & ~button);
                break;
            }
            case SDL_JOYDEVICEADDED:
                if (!SDL_IsGameController(event.jdevice.which)) {
                    if (SDL_Joystick* joystick = SDL_JoystickOpen(event.jdevice.which)) {
                        ESP_LOGI("host", "joystick: %s", SDL_JoystickName(joystick));
                        joysticks.push_back(joystick);
                    }
                }
                break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                if (SDL_GameControllerFromInstanceID(event.jbutton.which) == nullptr) {
                    const uint32_t button = brickJoystickButton(event.jbutton.button);
                    joystickButtons = event.type == SDL_JOYBUTTONDOWN ? (joystickButtons | button)
                                                                      : (joystickButtons & ~button);
                }
                break;
            case SDL_JOYHATMOTION:
                if (SDL_GameControllerFromInstanceID(event.jhat.which) == nullptr)
                    hatState = hatButtons(event.jhat.value);
                break;
            case SDL_JOYAXISMOTION:
                // Brick triggers: axis 2 = L2, axis 5 = R2.
                if (SDL_GameControllerFromInstanceID(event.jaxis.which) == nullptr) {
                    const uint32_t button = event.jaxis.axis == 2 ? ButtonL2 : event.jaxis.axis == 5 ? ButtonR2 : 0;
                    if (button != 0)
                        axisButtons = event.jaxis.value > 8000 ? (axisButtons | button) : (axisButtons & ~button);
                }
                break;
            case SDL_CONTROLLERAXISMOTION: {
                const uint32_t button = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT    ? ButtonL2
                                      : event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT ? ButtonR2
                                                                                             : 0;
                if (button != 0)
                    axisButtons = event.caxis.value > 8000 ? (axisButtons | button) : (axisButtons & ~button);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = event.type == SDL_MOUSEBUTTONDOWN;
                    mouseX = event.button.x;
                    mouseY = event.button.y;
                }
                break;
            case SDL_MOUSEMOTION:
                mouseX = event.motion.x;
                mouseY = event.motion.y;
                break;
            case SDL_WINDOWEVENT:
                overlayDirty = true;
                break;
            default:
                break;
            }
        }

        const uint32_t now = static_cast<uint32_t>(millis());
        gDeviceButtons = keyboardButtons | joystickButtons | hatState | axisButtons | controllerButtons;
        if (quitInjectedAtMs != 0 && now - quitInjectedAtMs > 5000) {
            ESP_LOGW("host", "app did not power off in time; exiting");
            gQuit = true;
        }

        // Targets published by the UI thread at the end of each frame.
        {
            const std::lock_guard lock{gTargetsMutex};
            if (gTargetsSerial != lastTargetsSerial) {
                lastTargetsSerial = gTargetsSerial;
                navigator.setTargets(gTargets, gTargetsScreen);
                overlayDirty = true;
            }
        }

        // Touch sources, by priority: mouse, script tap, controller navigation.
        ui::TouchContact contact{};
        int panelX = 0, panelY = 0;
        const uint32_t held = NextUiHost::buttonsHeld();
        if (mouseDown && presenter.toPanel(mouseX, mouseY, panelX, panelY)) {
            contact = {true, static_cast<uint16_t>(panelX), static_cast<uint16_t>(panelY)};
            navigator.reset(held);
        } else if (gScriptTouch.load()) {
            contact = {true, static_cast<uint16_t>(gScriptTouchX.load()), static_cast<uint16_t>(gScriptTouchY.load())};
            navigator.reset(held);
        } else {
            overlayDirty |= navigator.update(now, held);
            contact = navigator.contact();
        }
        {
            const std::lock_guard lock{gTouchMutex};
            gTouch = contact;
        }

        // Present when the firmware flushed a frame, the overlay moved, or at least every 250 ms.
        const uint32_t frame = NextUiDisplay::canvas().frame();
        const bool backlight = gBacklight.load();
        if (frame != lastFrame || overlayDirty || backlight != lastBacklight || now - lastRenderMs >= 250) {
            presenter.render(navigator);
            lastFrame = frame;
            lastBacklight = backlight;
            lastRenderMs = now;
            overlayDirty = false;
        }

        {
            std::unique_lock lock{gScreenshotMutex};
            while (!gScreenshots.empty()) {
                const std::string path = gScreenshots.front();
                lock.unlock();
                presenter.render(navigator);
                if (!presenter.saveScreenshot(path))
                    ESP_LOGW("host", "screenshot failed: %s", SDL_GetError());
                lock.lock();
                gScreenshots.pop_front();
            }
        }

        SDL_Delay(8);
    }

    for (SDL_GameController* controller: controllers)
        SDL_GameControllerClose(controller);
    for (SDL_Joystick* joystick: joysticks)
        SDL_JoystickClose(joystick);
    SDL_Quit();

    if (gRestart.load()) {
        std::vector<char*> args;
        for (std::string& arg: gArguments)
            args.push_back(arg.data());
        args.push_back(nullptr);
        execv("/proc/self/exe", args.data());
    }
    // The app thread may still be inside firmware code: leave without running static destructors.
    std::fflush(nullptr);
    std::_Exit(0);
}
