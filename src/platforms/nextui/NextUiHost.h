#pragma once

#include <cstdint>
#include <string_view>
#include <string>

#include "ui/Touch.h"

// Bridge between the firmware's Board::* interfaces (called from app threads) and the SDL host
// loop that owns the window, the controller and presentation (main thread).
namespace NextUiHost {

#ifndef RSVP_NEXTUI_PANEL_WIDTH
#define RSVP_NEXTUI_PANEL_WIDTH 640
#endif
#ifndef RSVP_NEXTUI_PANEL_HEIGHT
#define RSVP_NEXTUI_PANEL_HEIGHT 480
#endif
    constexpr uint16_t kPanelWidth = RSVP_NEXTUI_PANEL_WIDTH;
    constexpr uint16_t kPanelHeight = RSVP_NEXTUI_PANEL_HEIGHT;

    // Directory that plays the role of the SD card root ("/library", "/config", ...).
    const std::string& dataRoot();
    // Directory for host-only state (Preferences namespaces, logs).
    const std::string& stateRoot();

    // Logical buttons of the handheld, as read by the input bridge.
    enum Button : uint32_t {
        ButtonUp = 1U << 0,
        ButtonDown = 1U << 1,
        ButtonLeft = 1U << 2,
        ButtonRight = 1U << 3,
        ButtonA = 1U << 4,
        ButtonB = 1U << 5,
        ButtonX = 1U << 6,
        ButtonY = 1U << 7,
        ButtonL1 = 1U << 8,
        ButtonR1 = 1U << 9,
        ButtonL2 = 1U << 10,
        ButtonR2 = 1U << 11,
        ButtonSelect = 1U << 12,
        ButtonStart = 1U << 13,
        ButtonMenu = 1U << 14,
        ButtonPower = 1U << 15,
    };

    uint32_t buttonsHeld();
    // Virtual touch contact synthesized from the controller (focus navigation) or a mouse.
    bool touchContact(ui::TouchContact& contact);

    // Shows a QR code of text near the bottom of the screen (empty: none), e.g. the Sync address.
    void setQrCode(std::string_view text);
    void requestQuit();
    // After a suspend: forget held buttons (their releases may have been lost) and ignore input
    // until every button is released, so the wake press does nothing in the app.
    void afterWake();
    bool quitRequested();
    void setBacklight(bool on);
    void setBrightness(uint8_t percent);

} // namespace NextUiHost
