#pragma once

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

#include "ui/Touch.h"
#include "ui/Ui.h"

// Turns controller buttons into touches for a UI designed for a touchscreen.
//
//  * Screens with widgets: the D-pad moves a focus between them (spatial navigation); A taps the
//    focused widget. Left/Right on a slider or stepper changes its value.
//  * Holding A presses at the focus; the D-pad then drags (sliders, rotaries, swipes).
//  * Screens without widgets (the RSVP reader): the D-pad swipes from the screen centre, the same
//    gestures a finger makes (left/right scrub words, up/down change speed).
//  * L1/R1 always swipe left/right and L2/R2 swipe down/up from the centre.
class NextUiNavigator {
public:
    void setTargets(const std::vector<ui::Context::Target>& targets, uint8_t screen);
    // Returns true when the visible focus/pointer changed.
    bool update(uint32_t nowMs, uint32_t heldButtons);
    // Another touch source (mouse, test script) took over: drop gestures in flight.
    void reset(uint32_t heldButtons);

    ui::TouchContact contact() const {
        return {touched_, x_, y_, 0};
    }
    std::optional<ui::Rect> focusRect() const;
    std::optional<std::pair<int16_t, int16_t>> pointerVisible() const;

private:
    struct Step {
        uint32_t atMs = 0;
        bool touched = false;
        int16_t x = 0;
        int16_t y = 0;
    };
    struct Drag {
        bool active = false;
        uint32_t button = 0;
        int dx = 0;
        int dy = 0;
        float x = 0;
        float y = 0;
        float travelled = 0;
    };

    void moveFocus(int dx, int dy);
    void chooseFocus();
    void startDrag(uint32_t button, int dx, int dy, int16_t fromX, int16_t fromY, uint32_t nowMs);
    void queueTap(int16_t x, int16_t y, uint32_t nowMs);
    void queueNudge(int16_t fromX, int16_t toX, int16_t y, uint32_t nowMs);
    void setContact(bool touched, float x, float y);
    void directionPressed(uint32_t button, uint32_t nowMs);
    std::pair<int16_t, int16_t> focusCenter() const;

    std::vector<ui::Context::Target> targets_;
    uint8_t screen_ = 0xff;
    std::optional<ui::Context::Target> focus_;
    int16_t adjustX_ = -1;     // Virtual slider position while nudging with Left/Right.
    uint32_t previous_ = 0;
    uint32_t repeatButton_ = 0;
    uint32_t nextRepeatMs_ = 0;
    std::deque<Step> steps_;
    Drag drag_;
    bool pressing_ = false; // A held.
    uint32_t pressStartedMs_ = 0;
    uint32_t pressMoveStartedMs_ = 0;
    float pointerX_ = 0;
    float pointerY_ = 0;
    bool touched_ = false;
    uint16_t x_ = 0;
    uint16_t y_ = 0;
};
