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

enum class StartStopContext : uint8_t { BLOCKED, START, STOP };
enum class ButtonGesture : uint8_t { NONE, SHORT, LONG };

// Gesture qualifier only: the existing Machine remains the sole heater state machine.
class StartStopButton {
public:
    ButtonGesture update(bool raw_high, uint32_t now_ms, StartStopContext context);
private:
    DebouncedActiveLowButton button_{30};
    StartStopContext context_{StartStopContext::BLOCKED};
    uint32_t pressed_since_{}, released_since_{};
    bool initialized_{}, released_{}, release_timing_{}, active_{}, consumed_{};
};

}  // namespace sirokko
