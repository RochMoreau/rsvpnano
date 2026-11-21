#include "platforms/nextui/NextUiHelp.h"

#include <Arduino_GFX_Library.h>

#include <algorithm>

#include "fonts/UiFont6x9.h"
#include "platforms/nextui/NextUiHost.h"

namespace {

    constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    constexpr uint16_t kBackground = rgb565(0, 0, 0);
    constexpr uint16_t kPanel = rgb565(28, 28, 28);
    constexpr uint16_t kBorder = rgb565(70, 70, 70);
    constexpr uint16_t kText = rgb565(235, 235, 235);
    constexpr uint16_t kMuted = rgb565(150, 150, 150);
    constexpr uint16_t kAccent = rgb565(230, 40, 40);  // the reader's anchor-letter red
    constexpr uint16_t kFocus = rgb565(255, 196, 0);   // the navigator's focus frame

    struct Row {
        const char* key;
        const char* action;
    };

    struct Section {
        const char* title;
        const Row* rows;
        int count;
    };

    constexpr Row kReading[] = {
        {"A", "Hold to read, double-tap to play"},
        {"Start or X", "Play / pause"},
        {"Left/Right", "Step back / forward a word"},
        {"Up/Down", "Reading speed (hold to repeat)"},
        {"L1 / R1", "Previous / next sentence"},
        {"L2 / R2", "Previous / next paragraph"},
        {"B or Menu", "Open the menu"},
    };

    constexpr Row kMenus[] = {
        {"D-pad", "Move the yellow frame"},
        {"A", "Select   (A + D-pad: drag)"},
        {"B", "Back"},
        {"Left/Right", "Adjust a slider, change book"},
        {"L1 / R1", "Swipe left / right"},
    };

    constexpr Row kAnytime[] = {
        {"Y", "Show this screen"},
        {"Select", "Hold: standby"},
        {"Menu", "Menu   (hold: save and quit)"},
    };

    constexpr Section kSections[] = {
        {"READING", kReading, static_cast<int>(sizeof(kReading) / sizeof(kReading[0]))},
        {"MENUS", kMenus, static_cast<int>(sizeof(kMenus) / sizeof(kMenus[0]))},
        {"ANYTIME", kAnytime, static_cast<int>(sizeof(kAnytime) / sizeof(kAnytime[0]))},
    };

    void centered(Arduino_GFX& gfx, const char* text, int16_t y) {
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        gfx.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
        gfx.setCursor(static_cast<int16_t>((gfx.width() - static_cast<int16_t>(w)) / 2 - x1), y);
        gfx.print(text);
    }

    void draw(Arduino_GFX& gfx) {
        const int16_t width = gfx.width();
        const int16_t height = gfx.height();
        // Layout is designed for 640 x 480 and scales with the panel.
        const int16_t unit = std::max<int16_t>(1, std::min<int16_t>(width / 320, height / 240));
        const int16_t margin = 8 * unit;
        const int16_t rowHeight = 19 * unit / 2;
        const int16_t keyX = margin + 14 * unit;
        const int16_t actionX = keyX + 70 * unit;

        gfx.fillScreen(kBackground);
        gfx.fillRoundRect(margin, margin, width - 2 * margin, height - 2 * margin, 6 * unit, kPanel);
        gfx.drawRoundRect(margin, margin, width - 2 * margin, height - 2 * margin, 6 * unit, kBorder);

        gfx.setFont(u8g2_font_rsvpnano_ui_6x9_tf);
        gfx.setTextWrap(false);

        int16_t y = margin + 16 * unit;
        gfx.setTextSize(3 * unit / 2 > 0 ? 3 * unit / 2 : 1);
        gfx.setTextColor(kText);
        centered(gfx, "Controls", y);
        gfx.fillRect(width / 2 - 12 * unit, y + 4 * unit, 24 * unit, unit, kAccent);
        y += 10 * unit;

        gfx.setTextSize(unit);
        for (const Section& section: kSections) {
            y += 12 * unit;
            gfx.setTextColor(kAccent);
            gfx.setCursor(keyX - 6 * unit, y);
            gfx.print(section.title);
            for (int i = 0; i < section.count; ++i) {
                y += rowHeight;
                gfx.setTextColor(kFocus);
                gfx.setCursor(keyX, y);
                gfx.print(section.rows[i].key);
                gfx.setTextColor(kText);
                gfx.setCursor(actionX, y);
                gfx.print(section.rows[i].action);
            }
        }

        gfx.setTextColor(kMuted);
        centered(gfx, "Press A to continue", height - margin - 8 * unit);
    }

} // namespace

namespace NextUiHelp {

    const uint16_t* render() {
        static Arduino_Canvas canvas(NextUiHost::kPanelWidth, NextUiHost::kPanelHeight, nullptr);
        static bool ready = false;
        if (!ready) {
            if (!canvas.begin(GFX_SKIP_OUTPUT_BEGIN))
                return nullptr;
            draw(canvas);
            ready = true;
        }
        return canvas.getFramebuffer();
    }

} // namespace NextUiHelp
