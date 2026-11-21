#pragma once

#include <cstdint>

#include "board/BoardIndicators.h"

// The Brick's rumble motor, speaker, status LEDs and backlight, driven the way NextUI drives them
// (see NextUI's workspace/tg5040/platform/platform.c). Every function is safe to call on hardware
// without these devices: writes that fail are ignored.
//
// Environment (set in launch.sh or the shell):
//   RSVP_RUMBLE=0 / RSVP_SOUND=0 / RSVP_LEDS=0  turn a channel off.
//   RSVP_SYSFS_ROOT=DIR  prefix for /sys and /dev paths, to test on a desktop.
namespace NextUiFeedback {

    void begin();
    // Puts NextUI's own LED setup back (on exit and before sleeping).
    void end();

    // A focus or break phase finished: chime, three pulses, LEDs blink.
    void alarm();
    // Short pulse when going to sleep and waking up, if haptics are on in NextUI's settings.
    void sleepPulse();

    // Reflects what the reader is doing on the LEDs (called every loop; cheap when unchanged).
    void showIndicators(const Board::Indicators::State& state);

    // Raw panel backlight, 0-255 (/dev/disp). get returns -1 when unavailable.
    int backlight();
    void setBacklight(int level);

    // NextUI's suspend timeout in seconds (0 = never deep sleep), from minuisettings.txt.
    uint32_t suspendTimeoutSecs();

} // namespace NextUiFeedback
