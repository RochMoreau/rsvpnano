#pragma once

#include "ui/screens/Screens.h"

namespace screens::detail {

    inline constexpr int16_t kBackButtonHeight = 36;

#ifndef RSVP_WIDE_LAYOUT_MAX_HEIGHT
#define RSVP_WIDE_LAYOUT_MAX_HEIGHT 240
#endif
    // The tabbed layout (navigation rail, shelf library) was tuned for 640x172 strip panels;
    // boards with taller landscape screens can opt in by raising RSVP_WIDE_LAYOUT_MAX_HEIGHT.
    inline bool wideLayout(const ui::Context& ui) {
        return ui.width() >= 620 && ui.height() >= 150 && ui.height() <= RSVP_WIDE_LAYOUT_MAX_HEIGHT;
    }

    Action navigation(ui::Context& ui, Screen active, Screen& screen);
    ui::Rect content(ui::Context& ui);
    ui::Rect tabContent(ui::Context& ui);

} // namespace screens::detail
