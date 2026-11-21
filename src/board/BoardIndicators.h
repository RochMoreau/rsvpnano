#pragma once

#include <cstdint>

// Status lights. Boards with LEDs show what the reader is doing; the rest ignore it.
namespace Board::Indicators {

    enum class Activity : uint8_t {
        Idle,
        Reading,     // playback running; progress is the position in the book
        Focus,       // focus timer running; progress is the elapsed part of the phase
        Break,       // break timer running
        FocusPaused, // a focus session is open but not counting down
    };

    struct State {
        Activity activity = Activity::Idle;
        uint16_t progressPermille = 0;

        bool operator==(const State&) const = default;
    };

    // Called from the main loop on every iteration with the current state.
    void show(const State& state);

} // namespace Board::Indicators
