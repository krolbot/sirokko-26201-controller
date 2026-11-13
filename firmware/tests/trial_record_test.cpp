#include "control_fixture.hpp"
#include <iostream>
using namespace sirokko;
using namespace control_fixture;

// Synthetic sensor traces exercise acquisition and retention, not heater qualification.
static_assert(sizeof(TrialRecord) < 25 * 1024);
static void feed(TrialRecord &r, uint64_t t, State state, uint16_t pwm, int32_t c, bool active = true,
                 uint32_t faults = 0) {
    const auto i = input(t, c);
    r.observe(t, state,
              {state == State::RUN || state == State::IGNITION, pwm, state == State::IGNITION && t < 40000,
               state == State::RUN || state == State::IGNITION},
              i.sensors, faults, active);
}
static TrialRecord recorded(bool drift = false, bool stale = false) {
    TrialRecord r;
    r.begin(0, SafetyEnvelope{});
    for (uint64_t t = 0; t <= 200000; t += 100) {
        if (stale && t == 120000)
            continue;
        if (t < 30000)
            feed(r, t, State::PREHEAT, 0, 20000);
        else if (t < 42000)
            feed(r, t, State::IGNITION, 4014, t < 40000 ? 80000 : 160000);
        else
            feed(r, t, State::RUN, 2007, drift && t > 180000 ? 200000 - (t - 180000) * 2 : 200000);
        if (t == 50000)
            assert(r.select(2007));
        if (stale && t == 120100) {
            auto i = input(t, 200000);
            i.sensors.exhaust.sample_ms = t - kSensorStaleMs - 1;
            r.observe(t, State::RUN, {true, 2007, false, true}, i.sensors, 0, true);
        }
    }
    return r;
}
int main() {
    auto r = recorded();
    auto result = r.result();
    assert(result.reason == TrialRecordReason::READY && result.selected_pwm == 2007);
    assert(result.temperature_milli_c == 120000 && !result.complete && !result.saved);
    const auto count = result.samples;
    for (uint64_t t = 200100; t <= 400000; t += 100)
        feed(r, t, State::STOP_PURGE, 4014, 30000);
    feed(r, 400100, State::UNCONFIGURED, 0, 30000, false);
    assert(r.result().complete && r.result().samples == count);
    // Independent readers and elapsed idle time cannot consume or erase the record.
    for (unsigned i = 0; i < 10; ++i)
        assert(r.result().temperature_milli_c == 120000);
    feed(r, 900000, State::UNCONFIGURED, 0, 20000, false);
    assert(r.result().reason == TrialRecordReason::READY);
    auto profile = SafetyEnvelope{};
    profile.minimum_speed_code = result.selected_pwm;
    profile.minimum_run_exhaust_milli_c = result.temperature_milli_c;
    r.profile_changed(profile);
    assert(r.result().saved);
    profile.minimum_run_exhaust_milli_c += 1000;
    r.profile_changed(profile);
    assert(!r.result().saved && r.result().reason == TrialRecordReason::PROFILE_CHANGED);
    r.begin(900000, profile);
    assert(r.result().samples == 0 && !r.result().selected_pwm && !r.result().saved);
    assert(TrialRecord{}.result().reason == TrialRecordReason::EMPTY);
    assert(recorded(true).result().reason == TrialRecordReason::SETTLING);
    assert(recorded(false, true).result().reason == TrialRecordReason::INVALID);
    auto interrupted = recorded();
    feed(interrupted, 200100, State::FAULT_PURGE, 4014, 200000, true, FAULT_EXHAUST);
    assert(interrupted.result().reason == TrialRecordReason::INVALID);
    TrialRecord full;
    full.begin(0, SafetyEnvelope{});
    for (uint64_t t = 0; t < 205000; t += 100)
        feed(full, t, State::RUN, t % 200 ? 2000 : 2001, 200000);
    assert(full.result().reason == TrialRecordReason::FULL && full.result().samples == 2048);

    // End-to-end control ticks with no browser, Wi-Fi, recorder callback or network polling.
    Controller c(SafetyEnvelope{}, Settings{}, 42, Startup::CLEAN);
    Snapshot s;
    for (uint64_t t = 100; t <= 400000; t += 100) {
        auto i = input(t, t < 30200 ? 20000 : t < 200000 ? 200000 : 30000);
        i.local_button_valid = true;
        i.local_button_sample_ms = t;
        i.trial_request = t == 100;
        i.local_button_held = t == 200;
        i.trial_confirm = t == 40300;
        i.set_trial_pwm = t == 40400;
        i.trial_pwm_code = 2007;
        i.select_trial_pwm = t == 40500;
        i.stop = t == 200000;
        s = c.tick(i);
    }
    assert(!s.trial_active && s.trial_result.complete);
    assert(s.trial_result.reason == TrialRecordReason::READY && s.trial_result.selected_pwm == 2007);
    assert(s.trial_result.temperature_milli_c == 120000);
    auto same = c.snapshot(500000);
    assert(same.trial_result.samples == s.trial_result.samples &&
           same.trial_result.temperature_milli_c == 120000);
    profile = c.safety_envelope();
    profile.minimum_speed_code = 2007;
    profile.minimum_run_exhaust_milli_c = 120000;
    assert(c.install_safety_envelope(profile, false, 400100));
    assert(c.snapshot(400100).trial_result.saved);
    Controller reboot(profile, Settings{}, 43, Startup::CLEAN);
    assert(reboot.snapshot(0).trial_result.samples == 0);
    assert(reboot.safety_envelope().minimum_speed_code == 2007);
    std::cout << "PASS trial record: autonomous capture, selection, cooling retention, profile ACK, reboot, "
                 "invalid sensors, drift, fixed capacity\n";
}
