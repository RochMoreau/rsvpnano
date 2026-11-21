#pragma once

#include "platforms/nextui/NextUiHost.h"

// Linux handheld running NextUI (TrimUI Brick, tg5040). The UI renders into a logical RGB565
// panel (default 640x480, the wide tabbed layout) that the host scales to the 1024x768 screen.
namespace Board::Config {

    constexpr const char* BOARD_ID = "nextui_tg5040";
    constexpr const char* BOARD_LABEL = "TrimUI Brick (NextUI)";
    constexpr const char* OTA_ASSET_NAME = "";

    constexpr bool ENABLE_TOP_EDGE_MENU_SWIPE = true;
    constexpr bool ENABLE_BOTTOM_EDGE_QUICK_SETTINGS_SWIPE = true;
    constexpr bool READER_SINGLE_TAP_PAUSES_WHILE_LOCKED = true;
    constexpr bool TOUCH_READER_PLAYBACK_ENABLED = true;
    constexpr bool ENABLE_RESTRUCTURED_MENU = true;
    constexpr bool HAS_LIGHT_SLEEP_TOUCH_IRQ = false;

    constexpr int PANEL_NATIVE_WIDTH = NextUiHost::kPanelWidth;
    constexpr int PANEL_NATIVE_HEIGHT = NextUiHost::kPanelHeight;
    // Paint through an off-screen strip so the presenter never shows half-drawn widgets.
    constexpr int DISPLAY_WRITE_ALIGNMENT = 2;
    constexpr int DISPLAY_BUFFER_ROWS = 32;
    constexpr int DISPLAY_WIDTH = PANEL_NATIVE_WIDTH;
    constexpr int DISPLAY_HEIGHT = PANEL_NATIVE_HEIGHT;
    constexpr int READER_CHROME_MARGIN_X = 24;
    constexpr int READER_CHROME_MARGIN_TOP = 16;
    constexpr int READER_CHROME_MARGIN_BOTTOM = 16;
    constexpr int READER_BATTERY_MARGIN_X = READER_CHROME_MARGIN_X;
    constexpr int READER_BATTERY_MARGIN_TOP = READER_CHROME_MARGIN_TOP;

} // namespace Board::Config
