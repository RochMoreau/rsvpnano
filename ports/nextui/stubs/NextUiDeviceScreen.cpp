// Menu -> Device on NextUI. Replaces src/ui/screens/regular/DeviceScreen.cpp: USB transfer, RSS,
// over-the-air updates and storage encryption have no NextUI equivalent, so their buttons are left
// out instead of leading to "not available" screens. Storage and Companion sync remain.
#include "ui/screens/ScreenCommon.h"

#include <string>

namespace screens {

    Action device(ui::Context& ui, bool storageReady, size_t bookCount, settings::NvsEncryptionState encryptionState,
                  Screen& screen) {
        (void) encryptionState;
        if (const Action action = detail::navigation(ui, Screen::Device, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        constexpr int16_t gap = 4;
        constexpr int16_t statusHeight = 60;

        std::string storageStatus;
        if (storageReady) {
            storageStatus = std::to_string(bookCount);
            storageStatus += " ";
            storageStatus += ui.text(UiText::Items);
        } else {
            storageStatus = ui.text(UiText::Unavailable);
        }
        if (ui.setting({content.x, content.y, content.w, statusHeight}, ui.text(UiText::Storage), storageStatus,
                       ui::SettingLayout::Inline)) {
            return Action::StorageStatus;
        }

        const int16_t actionsY = static_cast<int16_t>(content.y + statusHeight + gap);
        const int16_t buttonHeight = static_cast<int16_t>((content.y + content.h - actionsY - gap) / 2);
        if (ui.button({content.x, actionsY, content.w, buttonHeight}, ui.text(UiText::CompanionSync), true,
                      ui::Icon::None, 2)) {
            return Action::CompanionSync;
        }

        const int16_t notesY = static_cast<int16_t>(actionsY + buttonHeight + gap * 3);
        const ui::Rect notes{static_cast<int16_t>(content.x + 8), notesY, static_cast<int16_t>(content.w - 16), 44};
        ui.label(notes, "Send books from a browser on the same Wi-Fi.", 2, ui::themes::ColorRole::Muted,
                 ui::TextAlign::Start, 2);
        ui.label({notes.x, static_cast<int16_t>(notes.y + notes.h + gap), notes.w, 44},
                 "To update, install a newer RSVP Nano pak.", 2, ui::themes::ColorRole::Muted, ui::TextAlign::Start, 2);
        return Action::None;
    }

    // Storage encryption is unavailable on NextUI; nothing links here, but send stray visits back.
    Action storageEncryption(ui::Context& ui, settings::NvsEncryptionState encryptionState, Screen& screen) {
        (void) ui;
        (void) encryptionState;
        if (screen == Screen::StorageEncryption)
            screen = Screen::Device;
        return Action::None;
    }

} // namespace screens
