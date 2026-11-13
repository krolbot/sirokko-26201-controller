#pragma once
#include "sirokko/control.hpp"
#include <cassert>
namespace control_fixture {
using namespace sirokko;
constexpr uint64_t kIgnitionAt = 120 + kPreheatMinimumMs;
constexpr uint64_t kRunAt = kIgnitionAt + 100 + kTemperatureConfirmationMs;
// Synthetic timings and temperatures; not settings for a real heater.
inline SafetyEnvelope envelope() {
    SafetyEnvelope e{};
    e.provisioned = true;
    e.ignition_timeout_ms = 5000;
    e.maximum_exhaust_milli_c = 300000;
    e.cooling_exhaust_milli_c = 30000;
    e.minimum_run_exhaust_milli_c = 40000;
    e.purge_speed_code = 1500;
    e.reference_speed_code = 2000;
    e.exploration_floor_code = 1000;
    assert(valid_safety_envelope(e));
    return e;
}
inline TickInput input(uint64_t t, int32_t exhaust = 20000, int32_t air = 20000) {
    TickInput i{};
    i.now_ms = t;
    i.cycle_record_ready = true;
    i.sensors.exhaust = {exhaust, t, 0, true};
    i.sensors.air = {air, t, 0, true};
    i.sensors.room = {20000, t, 0, true};
    return i;
}
inline LearnedOperatingModel model() {
    LearnedOperatingModel m{};
    m.valid = true;
    m.minimum_speed_code = 1200;
    m.maximum_speed_code = 2000;
    m.provenance = {envelope_fingerprint(envelope()), 1, 101, 1};
    m.measurements.elapsed_ms = 100;
    m.measurements.tested_point_count = 2;
    m.fingerprint = learned_model_fingerprint(m);
    return m;
}
inline Controller ignition(bool calibration = false) {
    Controller c(envelope(), Settings{}, 1, Startup::CLEAN);
    if (!calibration)
        assert(c.install_learned_model(model()));
    assert(c.tick(input(100)).state == State::IDLE);
    auto i = input(120);
    i.start = !calibration;
    i.start_commissioning = calibration;
    assert(c.tick(i).state == State::PREHEAT);
    assert(c.tick(input(220)).state == State::PREHEAT);
    auto s = c.tick(input(kIgnitionAt - 1));
    assert(s.state == State::PREHEAT && s.outputs.glow && !s.outputs.fuel && !s.outputs.fan_request);
    s = c.tick(input(kIgnitionAt));
    assert(s.state == State::IGNITION && s.outputs.fuel && s.outputs.glow);
    return c;
}
inline Snapshot confirm_ignition(Controller &c, int32_t exhaust = 50000, int32_t air = 30000) {
    Snapshot s{};
    for (uint64_t t = kIgnitionAt + 100; t <= kRunAt; t += 100) {
        s = c.tick(input(t, exhaust, air));
        assert(s.state == (t == kRunAt ? State::RUN : State::IGNITION));
        assert(s.outputs.fuel && s.outputs.glow == (t != kRunAt));
    }
    return s;
}
inline void off(const Snapshot &s) {
    assert(!s.outputs.fuel && !s.outputs.glow);
}
} // namespace control_fixture
