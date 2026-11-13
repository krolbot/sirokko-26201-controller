#include "control_fixture.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace sirokko;
using namespace control_fixture;
int main() {
    Controller disconnected({}, Settings{}, 7, Startup::CLEAN);
    auto detached = disconnected.snapshot(0);
    assert(detached.state == State::UNCONFIGURED && !detached.purge_active && !detached.outputs.fan_request);
    auto request = input(100);
    request.start = request.set_shutdown_timer = true;
    request.shutdown_after_ms = 10000;
    detached = disconnected.tick(request);
    assert(detached.state == State::UNCONFIGURED && !detached.faults && !detached.duration_active);
    assert(!detached.outputs.fuel && !detached.outputs.glow);
    Settings preview{};
    preview.room_setpoint_milli_c = 23000;
    assert(disconnected.update_settings(preview));
    request = {};
    request.now_ms = 200;
    request.enter_maintenance = true;
    assert(disconnected.tick(request).state == State::MAINTENANCE);
    request.stop = true;
    assert(disconnected.tick(request).state == State::UNCONFIGURED);

    assert(SafetyEnvelope{}.preheat_ms == 30000 && SafetyEnvelope{}.postpurge_ms == 150000);
    auto limits = envelope();
    limits.preheat_ms = 29999;
    assert(!valid_safety_envelope(limits));
    limits = envelope();
    limits.preheat_ms = 60001;
    assert(!valid_safety_envelope(limits));
    limits = envelope();
    limits.preheat_ms = 60000;
    assert(valid_safety_envelope(limits));
    limits = envelope();
    limits.postpurge_ms = 149999;
    assert(!valid_safety_envelope(limits));
    limits = envelope();
    limits.ignition_timeout_ms = 90000;
    assert(valid_safety_envelope(limits));
    ++limits.ignition_timeout_ms;
    assert(!valid_safety_envelope(limits));
    limits = envelope();
    limits.ignition_timeout_ms = kIgnitionGlowMs + kTemperatureConfirmationMs;
    assert(!valid_safety_envelope(limits));
    assert(valid_profile_draft(limits)); // Keep saved values editable after an update.
    Controller short_timeout(limits, Settings{}, 1, Startup::CLEAN);
    assert(short_timeout.tick(input(100)).state == State::UNCONFIGURED);
    ++limits.ignition_timeout_ms;
    assert(valid_safety_envelope(limits));
    Controller empty({}, Settings{}, 1, Startup::CLEAN);
    auto s = empty.tick(input(100));
    off(s);
    assert(s.start_block == Block::ENVELOPE_MISSING && !s.outputs.fan_request && !s.purge_active);
    Controller missing(envelope(), Settings{}, 1, Startup::CLEAN);
    s = missing.tick(input(100));
    assert(s.start_block == Block::NONE);
    auto i = input(120);
    i.start = true;
    s = missing.tick(i);
    assert(s.state == State::PREHEAT && s.outputs.glow && !s.outputs.fuel);
    Controller cold(envelope(), Settings{}, 1, Startup::CLEAN);
    s = cold.tick(input(100, -20000, -20000));
    assert(s.state == State::IDLE && s.start_block == Block::NONE);
    i = input(120, -20000, -20000);
    i.start = true;
    s = cold.tick(i);
    assert(s.state == State::PREHEAT);
    i = input(140, kThermocoupleMinimumMilliC - 1, 20000);
    s = cold.tick(i);
    off(s);
    assert(s.faults & FAULT_EXHAUST);
    Controller idle(envelope(), Settings{}, 1, Startup::CLEAN);
    idle.tick(input(100));
    CommandGate queued;
    const auto version = idle.snapshot(100).state_version;
    const auto pending = queued.issue(Action::START, 0, 100, version, 99);
    i = input(120);
    i.stop = true;
    s = idle.tick(i);
    assert(s.state == State::IDLE && s.state_version != version);
    assert(!queued.consume(pending, 140, s.state_version));
    const auto current = queued.issue(Action::START, 0, 140, s.state_version, 100);
    queued.issue(Action::SETTINGS, 0, 140, version, 101);
    assert(queued.consume(current, 150, s.state_version));
    const auto c = ignition();
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        auto copy = c;
        i = input(kIgnitionAt + 20);
        switch (scenario) {
        case 0:
            i.stop = true;
            i.start = true;
            break;
        case 1:
            i.sensors.exhaust.milli_c = 300000;
            break;
        case 2:
            i.sensors.air.milli_c = kProcessMeasurementCeilingMilliC;
            break;
        case 3:
            i.now_ms = kIgnitionAt + 680;
            i.sensors.exhaust.sample_ms = kIgnitionAt - 20;
            break;
        case 4:
            i.external_faults = FAULT_LOCAL_CONTROLS;
            break;
        case 5:
            i.sensors.exhaust.status = 1;
            break;
        case 6:
            i.now_ms = 300;
            break;
        case 7:
            i.sensors.air.sample_ms = kIgnitionAt + 21;
            break;
        }
        s = copy.tick(i);
        off(s);
        assert(s.state == State::STOP_PURGE || s.state == State::FAULT_PURGE);
        assert(s.outputs.fan_request);
    }
    // Glow duration is independent of exhaust temperature; cold exhaust still has a deadline.
    for (int32_t exhaust : {20000, 50000}) {
        auto timed = c;
        for (uint64_t t = kIgnitionAt + 100; t < kIgnitionAt + kIgnitionGlowMs; t += 100) {
            s = timed.tick(input(t, exhaust));
            assert(s.state == State::IGNITION && s.outputs.glow && s.outputs.fuel);
        }
        s = timed.tick(input(kIgnitionAt + kIgnitionGlowMs, exhaust));
        assert(s.state == State::IGNITION && !s.outputs.glow && s.outputs.fuel && s.outputs.fan_request);
        if (exhaust == 20000) {
            s = timed.tick(input(kIgnitionAt + envelope().ignition_timeout_ms));
            off(s);
            assert(s.state == State::FAULT_PURGE && (s.faults & FAULT_IGNITION_TIMEOUT));
        }
    }
    // STOP and sensor failures override both sides of the glow-off boundary.
    for (uint64_t offset : {kIgnitionGlowMs - 1, kIgnitionGlowMs, kIgnitionGlowMs + 1}) {
        for (bool stop : {false, true}) {
            auto interrupted = c;
            i = input(kIgnitionAt + offset);
            if (stop)
                i.stop = true;
            else
                i.sensors.exhaust.valid = false;
            s = interrupted.tick(i);
            off(s);
            assert(s.outputs.fan_request);
            assert(s.state == (stop ? State::STOP_PURGE : State::FAULT_PURGE));
        }
    }
    // Heated-air temperature is telemetry, not an ignition-rise or overheat threshold.
    auto slow_air = c;
    s = confirm_ignition(slow_air, 50000, 20000);
    assert(s.state == State::RUN && s.outputs.fuel && !s.faults);
    s = slow_air.tick(input(kRunAt + 20, 50000, 200000));
    assert(s.state == State::RUN && s.outputs.fuel && !s.faults);
    auto invalid_air = input(kRunAt + 40, 50000, kProcessMeasurementCeilingMilliC);
    s = slow_air.tick(invalid_air);
    assert((s.faults & FAULT_AIR) && !(s.faults & FAULT_OVER_TEMPERATURE));
    off(s);
    auto failed = c;
    s = failed.tick(input(kIgnitionAt + envelope().ignition_timeout_ms));
    off(s);
    assert(s.faults & FAULT_IGNITION_TIMEOUT);
    // The same guard drives fault-reset admission and the transition.
    s = failed.tick(input(kIgnitionAt + envelope().ignition_timeout_ms + kPostpurgeMinimumMs));
    assert(s.state == State::FAULT && s.reset_fault_block == Block::NONE);
    auto reset = input(kIgnitionAt + envelope().ignition_timeout_ms + 20 + kPostpurgeMinimumMs);
    reset.reset_fault = true;
    reset.external_faults = FAULT_LOCAL_CONTROLS;
    s = failed.tick(reset);
    assert(s.faults && s.reset_fault_block != Block::NONE);
    reset = input(kIgnitionAt + 140 + 2 * kPostpurgeMinimumMs);
    s = failed.tick(reset);
    assert(s.state == State::FAULT);
    reset.now_ms += 20;
    reset.sensors.air.valid = false;
    reset.reset_fault = true;
    s = failed.tick(reset);
    assert(s.state == State::FAULT && s.reset_fault_block == Block::SENSOR_INVALID);
    reset = input(reset.now_ms + 20);
    reset.reset_fault = true;
    s = failed.tick(reset);
    assert(s.state == State::IDLE && !s.faults);
    auto running = c;
    s = confirm_ignition(running);
    assert(s.state == State::RUN && s.outputs.fuel && !s.outputs.glow);
    auto flame = running;
    for (uint64_t t = kRunAt + 20; t <= kRunAt + 1020; t += 100) {
        s = flame.tick(input(t, 30000, 30000));
        if (t < kRunAt + 1020)
            assert(s.state == State::RUN && !s.faults);
    }
    off(s);
    assert(s.faults & FAULT_FLAME_LOST);
    i = input(kRunAt + 20, 50000, 30000);
    i.set_shutdown_timer = true;
    i.shutdown_after_ms = 100;
    running.tick(i);
    i = input(kRunAt + 120, 50000, 30000);
    i.set_shutdown_timer = true;
    i.shutdown_after_ms = 1000;
    s = running.tick(i);
    off(s);
    assert(s.state == State::STOP_PURGE); // Expiry wins over extension.
    i = input(kRunAt + 180, 60000, 30000);
    s = running.tick(i);
    assert(s.state == State::STOP_PURGE);
    s = running.tick(input(kRunAt + 120 + kPostpurgeMinimumMs));
    assert(s.state == State::IDLE);
    const uint64_t kConfirmationAt = kIgnitionAt + kIgnitionGlowMs;
    // A spike, duplicated sample or interrupted sequence cannot finish ignition.
    auto noisy = ignition();
    assert(noisy.tick(input(kConfirmationAt + 100, 50000)).state == State::IGNITION);
    auto repeated = input(kConfirmationAt + 500, 50000);
    repeated.sensors.exhaust.sample_ms = kConfirmationAt + 100;
    assert(noisy.tick(repeated).state == State::IGNITION);
    assert(noisy.tick(input(kConfirmationAt + 600, 20000)).state == State::IGNITION);
    for (uint64_t t = kConfirmationAt + 700; t < kConfirmationAt + 1700; t += 100)
        assert(noisy.tick(input(t, 50000)).state == State::IGNITION);
    assert(noisy.tick(input(kConfirmationAt + 1700, 50000)).state == State::RUN);
    // One short fall after ignition must not extinguish the heater.
    assert(noisy.tick(input(kConfirmationAt + 1800, 20000)).state == State::RUN);
    assert(noisy.tick(input(kConfirmationAt + 1900, 50000)).state == State::RUN);
    assert(noisy.tick(input(kConfirmationAt + 2000, 50000)).faults == 0);
    // A gap restarts confirmation rather than bridging missing observations.
    auto gap = ignition();
    gap.tick(input(kConfirmationAt + 100, 50000));
    gap.tick(input(kConfirmationAt + 800, 50000));
    assert(gap.tick(input(kConfirmationAt + 1100, 50000)).state == State::IGNITION);
    // Expiry takes priority even if the last reading would complete confirmation.
    auto deadline = ignition();
    const auto expires = kIgnitionAt + envelope().ignition_timeout_ms;
    for (uint64_t t = expires - 1000; t < expires; t += 100)
        assert(deadline.tick(input(t, 50000)).state == State::IGNITION);
    auto expired_start = deadline.tick(input(expires, 50000));
    off(expired_start);
    assert(expired_start.faults & FAULT_IGNITION_TIMEOUT);
    // The probe's measurement range is not the configurable overheat threshold.
    auto range_fault = ignition();
    auto invalid_probe = range_fault.tick(input(kIgnitionAt + 20, kProcessMeasurementCeilingMilliC));
    assert((invalid_probe.faults & FAULT_EXHAUST) && !(invalid_probe.faults & FAULT_OVER_TEMPERATURE));
    off(invalid_probe);
    auto gate = CommandGate{};
    auto a = gate.issue(Action::START, 10, 0, 1, 100);
    auto b = gate.issue(Action::SETTINGS, 11, 0, 1, 101);
    assert(a.valid && b.valid && gate.consume(a, 1, 1) && gate.consume(b, 1, 1));
    assert(!gate.consume(a, 2, 1));
    a = gate.issue(Action::START, 10, 0, 1, 100);
    auto forged = a;
    forged.params_hash++;
    assert(!gate.consume(forged, 1, 1));
    assert(gate.consume(a, 1, 1));
    a = gate.issue(Action::START, 10, 0, 1, 100);
    assert(!gate.consume(a, kCommandFreshnessMs, 1));
    a = gate.issue(Action::START, 10, 0, 1, 100);
    assert(!gate.consume(a, 1, 2));
    a = gate.issue(Action::START, 10, 0, 1, 100);
    gate.invalidate();
    assert(!gate.consume(a, 1, 1));
    for (unsigned n = 1; n <= 16; ++n)
        assert(gate.issue(Action::START, 0, 0, 1, n).valid);
    assert(!gate.issue(Action::START, 0, 0, 1, 17).valid);
    assert(gate.issue(Action::START, 0, 5000, 1, 17).valid);
    assert(!gate.issue(Action::START, 0, std::numeric_limits<uint64_t>::max(), 1, 18).valid);
    std::cout << "PASS control: first start from profile, STOP, sensors, faults, purge, timer, grants\n";
}
