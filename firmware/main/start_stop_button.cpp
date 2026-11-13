#include "start_stop_button.hpp"

namespace start_stop_button {
namespace {

bool elapsed(uint64_t now_ms, uint64_t since_ms, uint32_t duration_ms) {
    return now_ms - since_ms >= duration_ms;
}

bool is_stop_target(const Snapshot &snapshot) {
    switch (snapshot.state) {
    case sirokko::State::PREHEAT:
    case sirokko::State::IGNITION:
    case sirokko::State::RUN:
        return true;
    default:
        return false;
    }
}

bool is_start_target(const Snapshot &snapshot) {
    return snapshot.state == sirokko::State::IDLE && snapshot.start_block == sirokko::Block::NONE;
}

} // namespace

Button::Button(Executor executor, Timing timing) : executor_(executor), timing_(timing) {}

void Button::reset(bool pressed, uint64_t now_ms) {
    last_dispatch_ = Dispatch::NONE;
    have_last_poll_ = true;
    last_poll_ms_ = now_ms;
    rebaseline(pressed, now_ms);
}

void Button::rebaseline(bool pressed, uint64_t now_ms) {
    phase_started_ms_ = now_ms;
    release_resume_ = Phase::REQUIRE_RELEASE;
    intent_ = Intent::NONE;
    source_state_version_ = 0;
    held_sample_ = {now_ms, pressed, false};
    // A low level at reset/gap is indistinguishable from a held boot button: require a release.
    phase_ = pressed ? Phase::REQUIRE_RELEASE : Phase::RELEASE_DEBOUNCE;
}

void Button::begin_release_debounce(Phase resume, uint64_t now_ms) {
    release_resume_ = resume;
    phase_ = Phase::RELEASE_DEBOUNCE;
    phase_started_ms_ = now_ms;
}

void Button::latch_press_intent() {
    Snapshot snapshot{};
    if (!executor_.snapshot || !executor_.snapshot(executor_.context, &snapshot)) {
        intent_ = Intent::FAILED;
        return;
    }

    // The action is chosen at the first observed low level, never at dispatch time.
    source_state_version_ = snapshot.state_version;
    if (snapshot.trial_mode != sirokko::TrialMode::NONE) {
        intent_ = Intent::HOLD;
    } else if (is_stop_target(snapshot)) {
        intent_ = Intent::STOP;
    } else if (is_start_target(snapshot)) {
        intent_ = Intent::START;
    } else {
        intent_ = Intent::REJECTED;
    }
}

void Button::dispatch_latched_intent() {
    switch (intent_) {
    case Intent::START:
        last_dispatch_ = executor_.start && executor_.start(executor_.context, source_state_version_)
                             ? Dispatch::START
                             : Dispatch::FAILED;
        break;
    case Intent::STOP:
        last_dispatch_ =
            executor_.stop && executor_.stop(executor_.context) ? Dispatch::STOP : Dispatch::FAILED;
        break;
    case Intent::HOLD:
        last_dispatch_ = Dispatch::NONE;
        break;
    case Intent::REJECTED:
        last_dispatch_ = Dispatch::REJECTED;
        break;
    case Intent::FAILED:
    case Intent::NONE:
        last_dispatch_ = Dispatch::FAILED;
        break;
    }
    // One physical press can dispatch once. Stable release is required before the next press.
    phase_ = Phase::LOCKED;
}

void Button::poll(bool pressed, uint64_t now_ms) {
    if (!have_last_poll_) {
        reset(pressed, now_ms);
        return;
    }
    if (now_ms < last_poll_ms_ || now_ms - last_poll_ms_ > timing_.maximum_poll_gap_ms) {
        last_poll_ms_ = now_ms;
        rebaseline(pressed, now_ms);
        return;
    }
    last_poll_ms_ = now_ms;

    switch (phase_) {
    case Phase::REQUIRE_RELEASE:
        if (!pressed)
            begin_release_debounce(Phase::READY, now_ms);
        break;
    case Phase::RELEASE_DEBOUNCE:
        if (pressed) {
            phase_ = release_resume_;
        } else if (elapsed(now_ms, phase_started_ms_, timing_.debounce_ms)) {
            phase_ = Phase::READY;
            phase_started_ms_ = now_ms;
        }
        break;
    case Phase::READY:
        if (pressed) {
            last_dispatch_ = Dispatch::NONE;
            phase_ = Phase::PRESS_DEBOUNCE;
            phase_started_ms_ = now_ms;
            latch_press_intent();
        }
        break;
    case Phase::PRESS_DEBOUNCE:
        if (!pressed) {
            intent_ = Intent::NONE;
            phase_ = Phase::READY;
        } else if (elapsed(now_ms, phase_started_ms_, timing_.debounce_ms)) {
            dispatch_latched_intent();
        }
        break;
    case Phase::LOCKED:
        if (!pressed)
            begin_release_debounce(Phase::LOCKED, now_ms);
        break;
    }
    // Release invalidates the hold immediately; a new held level requires debounce.
    held_sample_ = {now_ms, phase_ == Phase::LOCKED && pressed,
                    phase_ == Phase::READY || phase_ == Phase::LOCKED};
}

} // namespace start_stop_button
