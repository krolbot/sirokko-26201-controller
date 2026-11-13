#pragma once

#include <cstdint>

#include "sirokko/control.hpp"

namespace start_stop_button {

struct Timing {
    uint32_t debounce_ms;
    uint32_t polling_ms;
    uint32_t maximum_poll_gap_ms;
};

// KEY0 is a short, debounced press. It deliberately has no hold threshold.
constexpr Timing kTiming{
    .debounce_ms = 30,
    .polling_ms = 10,
    .maximum_poll_gap_ms = 100,
};

struct Snapshot {
    sirokko::State state{sirokko::State::RECOVERY_PURGE};
    sirokko::Block start_block{sirokko::Block::ENVELOPE_MISSING};
    uint64_t state_version{};
    bool calibration_active{};
};

struct Executor {
    void *context{};
    bool (*snapshot)(void *context, Snapshot *snapshot){};
    bool (*start)(void *context, uint64_t source_state_version){};
    // `operator_combustion_problem` is true only for a STOP latched while calibration is active.
    bool (*stop)(void *context, bool operator_combustion_problem){};
};

enum class Dispatch : uint8_t { NONE, START, STOP, REJECTED, FAILED };

class Button {
  public:
    explicit Button(Executor executor, Timing timing = kTiming);

    // `pressed` is the already-normalized active-low GPIO input.
    // A press held at boot must be released before a new press can dispatch.
    void reset(bool pressed, uint64_t now_ms);
    void poll(bool pressed, uint64_t now_ms);
    Dispatch last_dispatch() const { return last_dispatch_; }

  private:
    enum class Phase : uint8_t {
        REQUIRE_RELEASE,
        RELEASE_DEBOUNCE,
        READY,
        PRESS_DEBOUNCE,
        LOCKED,
    };

    enum class Intent : uint8_t { NONE, START, STOP, REJECTED, FAILED };

    void rebaseline(bool pressed, uint64_t now_ms);
    void begin_release_debounce(Phase resume, uint64_t now_ms);
    void latch_press_intent();
    void dispatch_latched_intent();

    Executor executor_;
    Timing timing_;
    Phase phase_{Phase::REQUIRE_RELEASE};
    Phase release_resume_{Phase::REQUIRE_RELEASE};
    Intent intent_{Intent::NONE};
    uint64_t source_state_version_{};
    bool combustion_problem_{};
    uint64_t phase_started_ms_{};
    uint64_t last_poll_ms_{};
    bool have_last_poll_{};
    Dispatch last_dispatch_{Dispatch::NONE};
};

} // namespace start_stop_button
