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

}  // namespace sirokko
