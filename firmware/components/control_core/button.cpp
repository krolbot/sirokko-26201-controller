#include "sirokko/button.hpp"

namespace sirokko {

bool DebouncedActiveLowButton::update(bool raw_high, uint32_t now_ms) {
    const bool raw_pressed = !raw_high;
    if (!initialized_) {
        initialized_ = true;
        candidate_pressed_ = raw_pressed;
        stable_pressed_ = raw_pressed;
        candidate_since_ms_ = now_ms;
        release_seen_ = !raw_pressed;
        return false;
    }
    if (raw_pressed != candidate_pressed_) {
        candidate_pressed_ = raw_pressed;
        candidate_since_ms_ = now_ms;
    }
    if (candidate_pressed_ == stable_pressed_ ||
        static_cast<uint32_t>(now_ms - candidate_since_ms_) < debounce_ms_) {
        return false;
    }
    stable_pressed_ = candidate_pressed_;
    if (!stable_pressed_) {
        release_seen_ = true;
        return false;
    }
    return release_seen_;
}

ButtonGesture StartStopButton::update(bool raw_high, uint32_t now, StartStopContext context) {
    const bool was_pressed = button_.pressed();
    const bool press = button_.update(raw_high, now);
    if (!initialized_ || context != context_) {
        initialized_ = true;
        context_ = context;
        // Never carry a held gesture across recovery, inhibit or START/STOP roles.
        if (!raw_high || was_pressed || button_.pressed()) {
            active_ = released_ = release_timing_ = false;
            consumed_ = true;
        }
    }
    if (press) {
        active_ = released_ && context != StartStopContext::BLOCKED;
        consumed_ = false;
        pressed_since_ = now;
        released_ = false;
    }
    if (!raw_high) release_timing_ = false;
    else if (!release_timing_) { release_timing_ = true; released_since_ = now; }

    ButtonGesture result = ButtonGesture::NONE;
    if (active_ && !consumed_ && !raw_high && button_.pressed() &&
        static_cast<uint32_t>(now - pressed_since_) >= 1500) {
        result = ButtonGesture::LONG;
        consumed_ = true;
    }
    if (was_pressed && !button_.pressed()) {
        if (active_ && !consumed_ && static_cast<uint32_t>(released_since_ - pressed_since_) < 1500)
            result = ButtonGesture::SHORT;
        active_ = false;
    }
    if (raw_high && !button_.pressed() && static_cast<uint32_t>(now - released_since_) >= 30)
        released_ = true;
    return result;
}

}  // namespace sirokko
