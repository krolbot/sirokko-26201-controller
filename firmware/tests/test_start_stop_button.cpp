#include "start_stop_button.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

using namespace sirokko;
using namespace start_stop_button;
using ButtonSnapshot = start_stop_button::Snapshot;

namespace {

struct FakeRuntime {
    ButtonSnapshot snapshot{};
    bool snapshot_available{true};
    bool start_result{true};
    bool stop_result{true};
    unsigned snapshots{};
    unsigned starts{};
    unsigned stops{};
    uint64_t issued_for_version{};
    bool combustion_problem{};
};

bool read_snapshot(void* context, ButtonSnapshot* snapshot) {
    auto& fake = *static_cast<FakeRuntime*>(context);
    ++fake.snapshots;
    if (!fake.snapshot_available) return false;
    *snapshot = fake.snapshot;
    return true;
}

bool issue_and_submit_start(void* context, uint64_t expected_version) {
    auto& fake = *static_cast<FakeRuntime*>(context);
    ++fake.starts;
    fake.issued_for_version = expected_version;
    return fake.start_result;
}

bool runtime_stop_path(void* context, bool operator_combustion_problem) {
    auto& fake = *static_cast<FakeRuntime*>(context);
    ++fake.stops;
    fake.combustion_problem = operator_combustion_problem;
    return fake.stop_result;
}

Executor executor(FakeRuntime& fake) {
    return {&fake, read_snapshot, issue_and_submit_start, runtime_stop_path};
}

void settle_released(Button& button, uint64_t& now) {
    button.reset(false, now);
    now += kTiming.debounce_ms;
    button.poll(false, now);
}

void release(Button& button, uint64_t& now) {
    ++now;
    button.poll(false, now);
    now += kTiming.debounce_ms;
    button.poll(false, now);
}

void short_press(Button& button, uint64_t& now) {
    ++now;
    button.poll(true, now);
    now += kTiming.debounce_ms;
    button.poll(true, now);
}

ButtonSnapshot idle(uint64_t version = 7) {
    ButtonSnapshot value{};
    value.state = State::IDLE;
    value.start_block = Block::NONE;
    value.state_version = version;
    return value;
}

void test_start_dispatches_after_short_debounce_with_source_version() {
    FakeRuntime fake{};
    fake.snapshot = idle(42);
    Button button(executor(fake));
    uint64_t now = 0;
    settle_released(button, now);
    short_press(button, now);
    assert(button.last_dispatch() == Dispatch::START);
    assert(fake.snapshots == 1 && fake.starts == 1 && fake.stops == 0);
    assert(fake.issued_for_version == 42);
}

void test_stop_intent_is_latched_at_press_start() {
    FakeRuntime fake{};
    fake.snapshot = idle(10);
    fake.snapshot.state = State::RUN;
    fake.snapshot.start_block = Block::BUSY;
    Button button(executor(fake));
    uint64_t now = 0;
    settle_released(button, now);

    ++now;
    button.poll(true, now);  // Intent becomes STOP here.
    fake.snapshot = idle(11);  // Existing run ended while the input was still debouncing.
    now += kTiming.debounce_ms;
    button.poll(true, now);

    assert(button.last_dispatch() == Dispatch::STOP);
    assert(fake.stops == 1 && fake.starts == 0 && fake.snapshots == 1);
    assert(!fake.combustion_problem);
}

void test_held_at_boot_requires_release_and_fresh_press() {
    FakeRuntime fake{};
    fake.snapshot = idle();
    Button button(executor(fake));
    uint64_t now = 0;
    button.reset(true, now);
    for (unsigned i = 0; i < 20; ++i) {
        now += kTiming.polling_ms;
        button.poll(true, now);
    }
    assert(fake.starts == 0 && fake.stops == 0);

    release(button, now);
    assert(fake.starts == 0 && fake.stops == 0);
    short_press(button, now);
    assert(fake.starts == 1);
}

void test_bounce_and_short_press_are_filtered() {
    FakeRuntime fake{};
    fake.snapshot = idle();
    Button button(executor(fake));
    uint64_t now = 0;
    settle_released(button, now);

    ++now;
    button.poll(true, now);
    now += kTiming.debounce_ms - 1;
    button.poll(false, now);
    assert(fake.starts == 0 && fake.stops == 0 && fake.snapshots == 1);

    short_press(button, now);
    assert(fake.starts == 1 && fake.stops == 0);
}

void test_single_shot_requires_release() {
    FakeRuntime fake{};
    fake.snapshot = idle();
    Button button(executor(fake));
    uint64_t now = 0;
    settle_released(button, now);
    short_press(button, now);
    assert(fake.starts == 1);

    for (unsigned i = 0; i < 100; ++i) {
        now += kTiming.polling_ms;
        button.poll(true, now);
    }
    assert(fake.starts == 1 && fake.stops == 0);

    release(button, now);
    fake.snapshot = idle(8);
    short_press(button, now);
    assert(fake.starts == 2);
}

void test_purge_press_cannot_become_start_after_cooling() {
    for(State state : {State::STOP_PURGE,State::FAULT_PURGE,State::RECOVERY_PURGE}) {
        FakeRuntime fake{};fake.snapshot=idle(10);fake.snapshot.state=state;
        fake.snapshot.start_block=Block::BUSY;
        Button button(executor(fake));uint64_t now=0;settle_released(button,now);
        button.poll(true,++now); // Rejected intent captured before cooling ends.
        fake.snapshot=idle(11);
        now+=kTiming.debounce_ms;button.poll(true,now);
        assert(button.last_dispatch()==Dispatch::REJECTED && fake.starts==0);
        for(unsigned i=0;i<30;++i) {now+=kTiming.polling_ms;button.poll(true,now);}
        assert(fake.starts==0 && fake.stops==0);
        release(button,now);assert(fake.starts==0);
        short_press(button,now);assert(fake.starts==1 && fake.issued_for_version==11);
    }
}

void test_invalid_states_and_blocks_are_rejected() {
    const State rejected[] = {State::RECOVERY_PURGE, State::FAULT_PURGE, State::FAULT, State::MAINTENANCE,
                              State::STOP_PURGE};
    for (State state : rejected) {
        FakeRuntime fake{};
        fake.snapshot = idle();
        fake.snapshot.state = state;
        Button button(executor(fake));
        uint64_t now = 0;
        settle_released(button, now);
        short_press(button, now);
        assert(button.last_dispatch() == Dispatch::REJECTED);
        assert(fake.starts == 0 && fake.stops == 0);
    }

    const Block blocked_states[] = {
        Block::ENVELOPE_MISSING, Block::RECOVERY_PURGE, Block::SENSOR_INVALID,
        Block::INITIAL_THERMAL_STATE, Block::FAULT_LATCHED, Block::BUSY,
        Block::MAINTENANCE, Block::STALE_COMMAND,
    };
    for (Block block : blocked_states) {
        FakeRuntime blocked{};
        blocked.snapshot = idle();
        blocked.snapshot.start_block = block;
        Button button(executor(blocked));
        uint64_t now = 0;
        settle_released(button, now);
        short_press(button, now);
        assert(button.last_dispatch() == Dispatch::REJECTED && blocked.starts == 0 && blocked.stops == 0);
    }

    FakeRuntime delayed{};
    delayed.snapshot = idle();
    delayed.snapshot.delayed_armed = true;
    Button delayed_button(executor(delayed));
    uint64_t now = 0;
    settle_released(delayed_button, now);
    short_press(delayed_button, now);
    assert(delayed_button.last_dispatch() == Dispatch::STOP && delayed.stops == 1);
}

void test_calibration_stop_reports_combustion_problem() {
    FakeRuntime fake{};
    fake.snapshot = idle();
    fake.snapshot.state = State::RUN;
    fake.snapshot.calibration_active = true;
    Button button(executor(fake));
    uint64_t now = 0;
    settle_released(button, now);
    short_press(button, now);
    assert(button.last_dispatch() == Dispatch::STOP && fake.stops == 1);
    assert(fake.combustion_problem);
}

void test_failure_gap_and_clock_regression_require_release() {
    FakeRuntime unavailable{};
    unavailable.snapshot_available = false;
    Button button(executor(unavailable));
    uint64_t now = 0;
    settle_released(button, now);
    short_press(button, now);
    assert(button.last_dispatch() == Dispatch::FAILED && unavailable.snapshots == 1);
    unavailable.snapshot_available = true;
    unavailable.snapshot = idle();
    now += 10;
    button.poll(true, now);
    assert(unavailable.starts == 0 && unavailable.snapshots == 1);

    FakeRuntime timing{};
    timing.snapshot = idle();
    Button timing_button(executor(timing));
    now = 0;
    settle_released(timing_button, now);
    ++now;
    timing_button.poll(true, now);
    now += kTiming.maximum_poll_gap_ms + 1;
    timing_button.poll(true, now);
    assert(timing.starts == 0 && timing.stops == 0);
    timing_button.poll(true, now - 1);
    assert(timing.starts == 0 && timing.stops == 0);
}

void test_state_changes_before_debounce_never_retarget() {
    FakeRuntime fake{};
    fake.snapshot = idle(10);
    fake.snapshot.state = State::RUN;
    fake.snapshot.start_block = Block::BUSY;
    Button button(executor(fake));
    uint64_t now = 0;
    settle_released(button, now);

    ++now;
    button.poll(true, now);
    now += 15;
    button.poll(true, now);
    fake.snapshot = idle(11);  // The existing run ended while KEY0 remained down.
    now += 15;
    button.poll(true, now);
    assert(fake.starts == 0 && fake.stops == 1);

    FakeRuntime blocked{};
    blocked.snapshot = idle(20);
    blocked.snapshot.start_block = Block::ENVELOPE_MISSING;
    Button blocked_button(executor(blocked));
    now = 0;
    settle_released(blocked_button, now);
    ++now;
    blocked_button.poll(true, now);
    blocked.snapshot = idle(21);
    now += kTiming.debounce_ms;
    blocked_button.poll(true, now);
    assert(blocked.starts == 0 && blocked.stops == 0);
    assert(blocked_button.last_dispatch() == Dispatch::REJECTED);
}

}  // namespace

int main() {
    test_start_dispatches_after_short_debounce_with_source_version();
    test_stop_intent_is_latched_at_press_start();
    test_held_at_boot_requires_release_and_fresh_press();
    test_bounce_and_short_press_are_filtered();
    test_single_shot_requires_release();
    test_purge_press_cannot_become_start_after_cooling();
    test_invalid_states_and_blocks_are_rejected();
    test_calibration_stop_reports_combustion_problem();
    test_failure_gap_and_clock_regression_require_release();
    test_state_changes_before_debounce_never_retarget();
    std::cout << "PASS: KEY0 short press, debounce, boot lockout, intent latch and calibration reason\n";
}
