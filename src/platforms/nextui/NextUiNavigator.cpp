#include "platforms/nextui/NextUiNavigator.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include "platforms/nextui/NextUiHost.h"

#include <esp_log.h>

namespace {

    using namespace NextUiHost;

    constexpr uint32_t kDirections = ButtonUp | ButtonDown | ButtonLeft | ButtonRight;
    constexpr uint32_t kRepeatDelayMs = 380;
    constexpr uint32_t kRepeatIntervalMs = 110;
    constexpr uint32_t kMinimumPressMs = 50;
    constexpr uint32_t kStepMs = 24;
    constexpr float kSwipeStartDistance = 46.0f; // Above the reader's 40 px swipe threshold.
    constexpr float kSwipeFastSpeed = 12.0f;
    constexpr float kDragSpeed = 3.0f;
    constexpr float kDragMaxSpeed = 9.0f;
    constexpr int16_t kEdgeMargin = 6;

    int16_t centerX(const ui::Rect& rect) {
        return static_cast<int16_t>(rect.x + rect.w / 2);
    }
    int16_t centerY(const ui::Rect& rect) {
        return static_cast<int16_t>(rect.y + rect.h / 2);
    }

    void directionOf(uint32_t button, int& dx, int& dy) {
        dx = button == ButtonLeft ? -1 : button == ButtonRight ? 1 : 0;
        dy = button == ButtonUp ? -1 : button == ButtonDown ? 1 : 0;
    }

    bool sameTarget(const ui::Context::Target& a, const ui::Context::Target& b) {
        return a.rect == b.rect && a.role == b.role;
    }

} // namespace

void NextUiNavigator::setTargets(const std::vector<ui::Context::Target>& targets, uint8_t screen) {
    ESP_LOGD("nav", "targets screen=%u count=%zu", screen, targets.size());
    const bool screenChanged = screen != screen_;
    const std::optional<ui::Context::Target> previous = focus_;
    targets_ = targets;
    screen_ = screen;
    focus_.reset();
    if (targets_.empty())
        return;
    if (previous) {
        // The same widget survives (e.g. a navigation tab that switched the screen): keep it.
        for (const auto& target: targets_) {
            if (sameTarget(target, *previous)) {
                if (target.valueX != previous->valueX)
                    adjustX_ = -1;
                focus_ = target;
                return;
            }
        }
    }
    if (previous && !screenChanged) {
        // The layout changed under the focus (e.g. a list page): stay as close as possible.
        const int px = centerX(previous->rect), py = centerY(previous->rect);
        long best = std::numeric_limits<long>::max();
        for (const auto& target: targets_) {
            const long ddx = centerX(target.rect) - px, ddy = centerY(target.rect) - py;
            const long distance = ddx * ddx + ddy * ddy;
            if (distance < best) {
                best = distance;
                focus_ = target;
            }
        }
        adjustX_ = -1;
        return;
    }
    chooseFocus();
}

void NextUiNavigator::chooseFocus() {
    adjustX_ = -1;
    // Reading order (top row first, then left to right), preferring content over the navigation
    // rail that hugs the left edge, so opening the menu lands on "resume book".
    const bool hasContent = std::any_of(targets_.begin(), targets_.end(), [](const auto& target) {
        return target.rect.x > 0;
    });
    const ui::Context::Target* best = nullptr;
    for (const auto& target: targets_) {
        if (hasContent && target.rect.x <= 0)
            continue;
        if (best == nullptr) {
            best = &target;
            continue;
        }
        const int row = target.rect.y / 12, bestRow = best->rect.y / 12;
        if (row < bestRow || (row == bestRow && target.rect.x < best->rect.x))
            best = &target;
    }
    if (best != nullptr)
        focus_ = *best;
}

void NextUiNavigator::moveFocus(int dx, int dy) {
    if (!focus_) {
        chooseFocus();
        return;
    }
    const ui::Rect from = focus_->rect;
    const int fx = centerX(from), fy = centerY(from);
    const ui::Context::Target* best = nullptr;
    long bestScore = std::numeric_limits<long>::max();
    for (const auto& target: targets_) {
        if (sameTarget(target, *focus_))
            continue;
        const ui::Rect& rect = target.rect;
        const int tx = centerX(rect), ty = centerY(rect);
        // Must lie ahead in the pressed direction.
        if ((static_cast<long>(tx - fx) * dx + static_cast<long>(ty - fy) * dy) <= 2)
            continue;
        // Gap between facing edges, and whether the two widgets share a row (or column).
        long gap = 0;
        bool aligned = false;
        long across = 0;
        if (dx != 0) {
            gap = dx > 0 ? rect.x - (from.x + from.w) : from.x - (rect.x + rect.w);
            aligned = rect.y < from.y + from.h && from.y < rect.y + rect.h;
            across = std::abs(ty - fy);
        } else {
            gap = dy > 0 ? rect.y - (from.y + from.h) : from.y - (rect.y + rect.h);
            aligned = rect.x < from.x + from.w && from.x < rect.x + rect.w;
            across = std::abs(tx - fx);
        }
        gap = std::max<long>(0, gap);
        // Widgets in the same row/column always win; otherwise the nearest diagonal neighbour.
        const long score = aligned ? gap * 4 + across : 100000 + gap * 4 + across * 2;
        if (score < bestScore) {
            bestScore = score;
            best = &target;
        }
    }
    if (best != nullptr) {
        focus_ = *best;
        adjustX_ = -1;
    }
}

std::pair<int16_t, int16_t> NextUiNavigator::focusCenter() const {
    if (focus_)
        return {centerX(focus_->rect), centerY(focus_->rect)};
    return {static_cast<int16_t>(kPanelWidth / 2), static_cast<int16_t>(kPanelHeight / 2)};
}

void NextUiNavigator::setContact(bool touched, float x, float y) {
    touched_ = touched;
    x_ = static_cast<uint16_t>(std::clamp<float>(std::round(x), 0, kPanelWidth - 1));
    y_ = static_cast<uint16_t>(std::clamp<float>(std::round(y), 0, kPanelHeight - 1));
}

void NextUiNavigator::queueTap(int16_t x, int16_t y, uint32_t nowMs) {
    steps_.push_back({nowMs, true, x, y});
    steps_.push_back({nowMs + kMinimumPressMs + kStepMs, false, x, y});
}

void NextUiNavigator::queueSwipe(int dx, int dy, uint32_t nowMs) {
    constexpr int kDistance = 64; // Past the reader's 40 px swipe threshold.
    const int16_t x = kPanelWidth / 2, y = kPanelHeight / 2;
    steps_.push_back({nowMs, true, x, y});
    for (int i = 1; i <= 4; ++i)
        steps_.push_back({nowMs + static_cast<uint32_t>(i) * kStepMs, true,
                          static_cast<int16_t>(x + dx * kDistance * i / 4), static_cast<int16_t>(y + dy * kDistance * i / 4)});
    steps_.push_back({nowMs + 5 * kStepMs, false, static_cast<int16_t>(x + dx * kDistance),
                      static_cast<int16_t>(y + dy * kDistance)});
}

void NextUiNavigator::queueNudge(int16_t fromX, int16_t toX, int16_t y, uint32_t nowMs) {
    steps_.push_back({nowMs, true, fromX, y});
    const int16_t middle = static_cast<int16_t>((fromX + toX) / 2);
    steps_.push_back({nowMs + kStepMs, true, middle, y});
    steps_.push_back({nowMs + 2 * kStepMs, true, toX, y});
    steps_.push_back({nowMs + 3 * kStepMs, false, toX, y});
}

void NextUiNavigator::startDrag(uint32_t button, int dx, int dy, int16_t fromX, int16_t fromY, uint32_t nowMs) {
    drag_ = {.active = true,
             .button = button,
             .dx = dx,
             .dy = dy,
             .x = static_cast<float>(fromX),
             .y = static_cast<float>(fromY),
             .travelled = 0};
    pressStartedMs_ = nowMs;
    setContact(true, drag_.x, drag_.y);
}

void NextUiNavigator::directionPressed(uint32_t button, uint32_t nowMs) {
    int dx = 0, dy = 0;
    directionOf(button, dx, dy);
    ESP_LOGD("nav", "direction %d,%d targets=%zu focus=%d,%d %dx%d role=%d value=%d", dx, dy, targets_.size(),
             focus_ ? focus_->rect.x : -1, focus_ ? focus_->rect.y : -1, focus_ ? focus_->rect.w : 0,
             focus_ ? focus_->rect.h : 0, focus_ ? static_cast<int>(focus_->role) : -1,
             focus_ ? focus_->valueX : -1);
    if (targets_.empty()) {
        // Gesture screens (the reader): swipe from the centre like a finger would. Left/Right keep
        // dragging while held (scrubbing); Up/Down are one swipe each, repeated while held (speed).
        if (dy != 0)
            queueSwipe(0, dy, nowMs);
        else
            startDrag(button, dx, dy, kPanelWidth / 2, kPanelHeight / 2, nowMs);
        return;
    }
    if (focus_ && dx != 0 && focus_->role == ui::Context::TargetRole::Step) {
        const ui::Rect& rect = focus_->rect;
        const int16_t inset = static_cast<int16_t>(std::min<int>(12, rect.w / 10));
        queueTap(dx < 0 ? static_cast<int16_t>(rect.x + inset) : static_cast<int16_t>(rect.x + rect.w - 1 - inset),
                 centerY(rect), nowMs);
        return;
    }
    if (focus_ && dx != 0 && focus_->role == ui::Context::TargetRole::Carousel) {
        // Select the neighbouring item: drag the content the opposite way by one item pitch.
        const ui::Rect& rect = focus_->rect;
        const int16_t pitch = static_cast<int16_t>(focus_->valueX > 0 ? focus_->valueX : rect.w / 6);
        const int16_t x = centerX(rect), y = centerY(rect);
        steps_.push_back({nowMs, true, x, y});
        for (int i = 1; i <= 4; ++i)
            steps_.push_back({nowMs + static_cast<uint32_t>(i) * kStepMs, true,
                              static_cast<int16_t>(x - dx * pitch * i / 4), y});
        steps_.push_back({nowMs + 5 * kStepMs, false, static_cast<int16_t>(x - dx * pitch), y});
        return;
    }
    if (focus_ && dx != 0 && focus_->role == ui::Context::TargetRole::Adjust) {
        const ui::Rect& rect = focus_->rect;
        const int16_t y = centerY(rect);
        if (focus_->valueX < 0) {
            // Relative control (rotary): a short drag from the centre moves one step.
            queueNudge(centerX(rect), static_cast<int16_t>(centerX(rect) + dx * 10), y, nowMs);
            return;
        }
        // Absolute control (slider): walk a virtual knob until the value changes.
        const int16_t delta = static_cast<int16_t>(std::max(4, rect.w / 20));
        if (adjustX_ < 0)
            adjustX_ = focus_->valueX;
        adjustX_ = static_cast<int16_t>(std::clamp<int>(adjustX_ + dx * delta, rect.x, rect.x + rect.w - 1));
        queueNudge(focus_->valueX, adjustX_, y, nowMs);
        return;
    }
    moveFocus(dx, dy);
}

void NextUiNavigator::reset(uint32_t heldButtons) {
    steps_.clear();
    drag_ = {};
    pressing_ = false;
    touched_ = false;
    previous_ = heldButtons;
    repeatButton_ = 0;
}

bool NextUiNavigator::update(uint32_t nowMs, uint32_t held) {
    const std::optional<ui::Rect> focusBefore = focusRect();
    const bool touchedBefore = touched_;
    const uint16_t xBefore = x_, yBefore = y_;
    const uint32_t pressed = held & ~previous_;
    const uint32_t released = previous_ & ~held;
    previous_ = held;

    // 1. Scripted taps and nudges own the contact until they finish.
    if (!steps_.empty()) {
        while (!steps_.empty() && static_cast<int32_t>(nowMs - steps_.front().atMs) >= 0) {
            const Step step = steps_.front();
            steps_.pop_front();
            setContact(step.touched, step.x, step.y);
        }
    }
    const bool busy = !steps_.empty();

    // 2. A: press and hold like a finger at the focus (or the last pointer position).
    if (!busy && !drag_.active && (pressed & ButtonA)) {
        const auto [fx, fy] = focusCenter();
        if (focus_ || pointerX_ <= 0) {
            pointerX_ = fx;
            pointerY_ = fy;
        }
        pressing_ = true;
        pressStartedMs_ = nowMs;
        pressMoveStartedMs_ = 0;
        setContact(true, pointerX_, pointerY_);
    }
    if (pressing_) {
        if (held & ButtonA) {
            const int dx = (held & ButtonLeft ? -1 : 0) + (held & ButtonRight ? 1 : 0);
            const int dy = (held & ButtonUp ? -1 : 0) + (held & ButtonDown ? 1 : 0);
            if (dx != 0 || dy != 0) {
                if (pressMoveStartedMs_ == 0)
                    pressMoveStartedMs_ = nowMs;
                const float speed =
                    std::min(kDragMaxSpeed, kDragSpeed + static_cast<float>(nowMs - pressMoveStartedMs_) / 120.0f);
                pointerX_ = std::clamp<float>(pointerX_ + dx * speed, 0, kPanelWidth - 1);
                pointerY_ = std::clamp<float>(pointerY_ + dy * speed, 0, kPanelHeight - 1);
            } else {
                pressMoveStartedMs_ = 0;
            }
            setContact(true, pointerX_, pointerY_);
        } else {
            pressing_ = false;
            const uint32_t heldFor = nowMs - pressStartedMs_;
            if (heldFor < kMinimumPressMs)
                steps_.push_back({pressStartedMs_ + kMinimumPressMs, false, static_cast<int16_t>(x_),
                                  static_cast<int16_t>(y_)});
            else
                setContact(false, pointerX_, pointerY_);
        }
    }

    // 3. Swipes (shoulders on screens with widgets; D-pad on gesture screens). In the reader the
    //    shoulders jump by sentence / paragraph instead (Input actions, see BoardPlatform.cpp).
    if (!busy && !pressing_ && !drag_.active && !targets_.empty()) {
        if (pressed & ButtonL1)
            startDrag(ButtonL1, -1, 0, kPanelWidth / 2, kPanelHeight / 2, nowMs);
        else if (pressed & ButtonR1)
            startDrag(ButtonR1, 1, 0, kPanelWidth / 2, kPanelHeight / 2, nowMs);
        else if (pressed & ButtonL2)
            startDrag(ButtonL2, 0, 1, kPanelWidth / 2, kPanelHeight / 2, nowMs);
        else if (pressed & ButtonR2)
            startDrag(ButtonR2, 0, -1, kPanelWidth / 2, kPanelHeight / 2, nowMs);
    }
    if (drag_.active && static_cast<int32_t>(nowMs - pressStartedMs_) >= 0) {
        const bool stillHeld = (held & drag_.button) != 0;
        if (!stillHeld && drag_.travelled >= kSwipeStartDistance) {
            drag_.active = false;
            setContact(false, drag_.x, drag_.y);
        } else {
            const float speed = drag_.travelled < kSwipeStartDistance
                                  ? kSwipeFastSpeed
                                  : std::min(kDragMaxSpeed, kDragSpeed + static_cast<float>(nowMs - pressStartedMs_) / 400.0f);
            const float nextX = drag_.x + drag_.dx * speed;
            const float nextY = drag_.y + drag_.dy * speed;
            const bool atEdge = nextX < kEdgeMargin || nextY < kEdgeMargin || nextX > kPanelWidth - 1 - kEdgeMargin
                             || nextY > kPanelHeight - 1 - kEdgeMargin;
            if (atEdge) {
                // Lift and start again from the centre so holding keeps scrubbing.
                setContact(false, drag_.x, drag_.y);
                const int16_t cx = kPanelWidth / 2, cy = kPanelHeight / 2;
                steps_.push_back({nowMs + 2 * kStepMs, true, cx, cy});
                drag_.x = cx;
                drag_.y = cy;
                drag_.travelled = 0;
                pressStartedMs_ = nowMs + 2 * kStepMs;
            } else {
                drag_.x = nextX;
                drag_.y = nextY;
                drag_.travelled += speed;
                setContact(true, drag_.x, drag_.y);
            }
        }
    }

    // 4. D-pad focus navigation with key repeat.
    if (!pressing_ && !drag_.active && steps_.empty()) {
        const uint32_t direction = pressed & kDirections;
        uint32_t fire = 0;
        if (direction != 0) {
            fire = direction & (~direction + 1); // lowest set bit
            repeatButton_ = fire;
            nextRepeatMs_ = nowMs + kRepeatDelayMs;
        } else if (repeatButton_ != 0 && (held & repeatButton_)
                   && (!targets_.empty() || (repeatButton_ & (ButtonUp | ButtonDown)) != 0)
                   && static_cast<int32_t>(nowMs - nextRepeatMs_) >= 0) {
            fire = repeatButton_;
            nextRepeatMs_ = nowMs + kRepeatIntervalMs;
        }
        if (fire != 0)
            directionPressed(fire, nowMs);
    }
    if (released & repeatButton_)
        repeatButton_ = 0;

    return focusBefore != focusRect() || touchedBefore != touched_ || (touched_ && (xBefore != x_ || yBefore != y_));
}

std::optional<ui::Rect> NextUiNavigator::focusRect() const {
    if (!focus_ || targets_.empty())
        return std::nullopt;
    return focus_->rect;
}

std::optional<std::pair<int16_t, int16_t>> NextUiNavigator::pointerVisible() const {
    if (!touched_)
        return std::nullopt;
    return std::pair<int16_t, int16_t>{static_cast<int16_t>(x_), static_cast<int16_t>(y_)};
}
