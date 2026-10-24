#pragma once

#include <cstdint>

namespace sirokko {

class DebouncedActiveLowButton {
public:
    explicit DebouncedActiveLowButton(uint32_t debounce_ms = 40) : debounce_ms_(debounce_ms) {}

    // raw_high=true means released/open. Returns one event on a qualified new press.
    bool update(bool raw_high, uint32_t now_ms);
    bool pressed() const { return stable_pressed_ && release_seen_; }
    bool release_seen() const { return release_seen_; }

private:
    uint32_t debounce_ms_;
    uint32_t candidate_since_ms_{};
    bool initialized_{};
    bool candidate_pressed_{};
    bool stable_pressed_{};
    bool release_seen_{};
};

}  // namespace sirokko
