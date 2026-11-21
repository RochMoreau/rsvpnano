// Settings -> Network & updates on NextUI. Replaces src/ui/screens/regular/NetworkSettingsScreen.cpp:
// NextUI owns Wi-Fi and updates come as a new pak, so the firmware's Wi-Fi picker, saved network,
// startup update check and release settings would only mislead. The screen shows the connection
// NextUI made and keeps Companion sync.
#include "ui/screens/ScreenCommon.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "platforms/nextui/NextUiNetwork.h"
#include "settings/SettingsStore.h"

namespace screens {

    Action NetworkScreen::draw(ui::Context& ui, settings::SettingsStore& store, Screen& screen) {
        (void) store;
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 4;
        constexpr int16_t backWidth = 56;
        const int16_t rowHeight = static_cast<int16_t>((content.h - gap * 3) / 4);
        const int16_t textX = static_cast<int16_t>(content.x + backWidth + gap * 3);
        const int16_t textWidth = static_cast<int16_t>(content.x + content.w - textX);

        if (ui.button({content.x, content.y, backWidth, rowHeight}, "<<"))
            screen = Screen::Settings;

        const std::string address = NextUiNetwork::address();
        constexpr int16_t line = 24;
        int16_t y = static_cast<int16_t>(content.y + 6);
        const auto text = [&](std::string_view value, ui::themes::ColorRole role, uint8_t lines = 1) {
            ui.label({textX, y, textWidth, static_cast<int16_t>(line * lines)}, value, 2, role, ui::TextAlign::Start,
                     lines);
            y = static_cast<int16_t>(y + line * lines);
        };

        text("Wi-Fi", ui::themes::ColorRole::Foreground);
        text(address.empty() ? "Not connected" : "Connected  " + address,
             address.empty() ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::Muted);
        text("Managed by NextUI: turn Wi-Fi on and pick a network in NextUI's settings.",
             ui::themes::ColorRole::Muted, 2);
        y = static_cast<int16_t>(y + line);
        text("Updates", ui::themes::ColorRole::Foreground);
        text("Install a newer RSVP Nano pak on the SD card.", ui::themes::ColorRole::Muted);

        const int16_t buttonHeight = std::min<int16_t>(rowHeight, 72);
        if (ui.button({content.x, static_cast<int16_t>(content.y + content.h - buttonHeight), content.w, buttonHeight},
                      ui.text(UiText::CompanionSync), !address.empty())) {
            return Action::CompanionSync;
        }
        return Action::None;
    }

    // The firmware's Wi-Fi picker is not reachable on NextUI; send anything that lands here back.
    void NetworkScreen::drawWifiScan(ui::Context& ui, settings::SettingsStore& store, Screen& screen) {
        (void) ui;
        (void) store;
        closeWifi();
        screen = Screen::NetworkSettings;
    }

} // namespace screens
