#include "control_fixture.hpp"
#include "persistence.hpp"
#include <iostream>
using namespace sirokko;
using namespace control_fixture;

int main() {
    // First configuration on a never-used ESP must not invent an interrupted run.
    unsigned writes = 0;
    auto saved_cycle = CycleRecordStatus::MISSING;
    const auto persist_clean = [&] {
        ++writes;
        saved_cycle = CycleRecordStatus::CLEAN;
        return true;
    };
    auto startup = startup_for_cycle(saved_cycle, false, persist_clean);
    assert(startup == Startup::CLEAN && writes == 1);
    Controller first({}, {}, 1, startup);
    assert(first.install_safety_envelope(envelope(), startup == Startup::RECOVER, 0));
    assert(first.snapshot(0).state == State::IDLE && !first.snapshot(0).outputs.fan_request);
    startup = startup_for_cycle(saved_cycle, true, persist_clean);
    Controller configured(envelope(), {}, 2, startup);
    assert(configured.snapshot(0).state == State::IDLE && writes == 1);
    for (bool profile_present : {false, true})
        for (auto cycle : {CycleRecordStatus::INTERRUPTED, CycleRecordStatus::INVALID})
            assert(startup_for_cycle(cycle, profile_present, persist_clean) == Startup::RECOVER);
    assert(startup_for_cycle(CycleRecordStatus::MISSING, true, persist_clean) == Startup::RECOVER);
    assert(writes == 1); // Existing/unknown cycle state is never overwritten as clean.
    assert(startup_for_cycle(CycleRecordStatus::MISSING, false, [] { return false; }) == Startup::RECOVER);

    // Losing the profile cannot erase evidence of an interrupted heating cycle.
    for (bool missing_time : {false, true}) {
        SafetyEnvelope partial{};
        if (missing_time)
            partial.postpurge_ms = 0;
        Controller interrupted(partial, {}, 3, Startup::RECOVER);
        auto s = interrupted.tick(input(0, 100000));
        assert(s.state == State::RECOVERY_PURGE && s.outputs.fan_request);
        off(s);
        assert(interrupted.tick(input(kPostpurgeMinimumMs - 1)).purge_active);
        auto no_data = input(kPostpurgeMinimumMs);
        no_data.sensors.exhaust.valid = false;
        assert(interrupted.tick(no_data).purge_active);
        s = interrupted.tick(input(kPostpurgeMinimumMs + 20));
        assert(s.state == State::UNCONFIGURED && !s.outputs.fan_request);
        off(s);
    }

    // Repeated cold boots after completed shutdown do not energize any output.
    for (unsigned boot = 1; boot <= 3; ++boot) {
        Controller cold(envelope(), {}, boot, Startup::CLEAN);
        auto s = cold.snapshot(0);
        assert(s.state == State::IDLE && !s.purge_active && s.purge_id == 0);
        assert(!s.outputs.fan_request && !s.outputs.glow && !s.outputs.fuel);
        TickInput no_sensors{};
        no_sensors.now_ms = 1;
        s = cold.tick(no_sensors);
        assert(s.state == State::IDLE && !s.outputs.fan_request);
        assert(cold.tick(input(100)).state == State::IDLE);
    }
    // No ignition or calibration can begin until storage has acknowledged the marker.
    for (const bool calibration : {false, true}) {
        Controller cold(envelope(), {}, 1, Startup::CLEAN);
        assert(cold.install_learned_model(model()));
        auto i = input(1);
        i.start = !calibration;
        i.start_commissioning = calibration;
        i.cycle_record_ready = false;
        auto s = cold.tick(i);
        assert(s.state == State::IDLE);
        assert(!s.outputs.fan_request && !s.outputs.glow && !s.outputs.fuel);
        i = input(2);
        i.start = !calibration;
        i.start_commissioning = calibration;
        i.cycle_record_ready = true;
        s = cold.tick(i);
        assert(s.state == State::PREHEAT && s.outputs.glow && !s.outputs.fan_request && !s.outputs.fuel);
    }
    // A failed pre-start storage write faults an idle controller without fake cooling.
    Controller failed(envelope(), {}, 1, Startup::CLEAN);
    auto i = input(1);
    i.start = true;
    i.cycle_record_ready = false;
    i.external_faults = FAULT_STORAGE;
    auto s = failed.tick(i);
    assert(s.state == State::FAULT && (s.faults & FAULT_STORAGE));
    assert(!s.outputs.fan_request && !s.outputs.glow && !s.outputs.fuel);

    // Recovery uses exactly the same adjustable time and exhaust threshold as normal cooling.
    for (uint64_t cooling_ms : {kPostpurgeMinimumMs, kPostpurgeMinimumMs + 30000}) {
        auto profile = envelope();
        profile.postpurge_ms = cooling_ms;
        profile.cooling_exhaust_milli_c = 40000;
        profile.minimum_run_exhaust_milli_c = 60000;
        Controller interrupted(profile, {}, 2, Startup::RECOVER);
        s = interrupted.snapshot(0);
        assert(s.state == State::RECOVERY_PURGE && s.purge_active && s.outputs.fan_request);
        assert(s.purge_minimum_remaining_ms == cooling_ms);
        assert(!s.outputs.fuel && !s.outputs.glow && !s.duration_active && !s.run_id);
        i = input(50);
        i.start = i.start_commissioning = i.stop = true;
        assert(interrupted.tick(i).state == State::RECOVERY_PURGE);
        s = interrupted.tick(input(cooling_ms - 1, 39999));
        assert(s.state == State::RECOVERY_PURGE && s.purge_minimum_remaining_ms == 1);
        s = interrupted.tick(input(cooling_ms, 40000));
        assert(s.state == State::RECOVERY_PURGE && s.purge_minimum_remaining_ms == 0);
        i = input(cooling_ms + 20);
        i.sensors.exhaust.valid = false;
        assert(interrupted.tick(i).state == State::RECOVERY_PURGE);
        i = input(cooling_ms + 40);
        i.sensors.air.sample_ms = 0;
        assert(interrupted.tick(i).state == State::RECOVERY_PURGE);
        s = interrupted.tick(input(cooling_ms + 60, 39999));
        assert(s.state == State::IDLE && !s.purge_active && !s.outputs.fan_request);
        assert(!s.outputs.fuel && !s.outputs.glow);
    }

    // Stopping an actual run still requires the entire cooling cycle.
    auto running = ignition();
    confirm_ignition(running, 60000, 30000);
    i = input(kRunAt + 20, 60000, 30000);
    i.stop = true;
    s = running.tick(i);
    assert(s.state == State::STOP_PURGE && s.purge_active);
    assert(running.tick(input(kRunAt + 21)).state == State::STOP_PURGE);
    s = running.tick(input(kRunAt + 20 + kPostpurgeMinimumMs));
    assert(s.state == State::IDLE && !s.outputs.fan_request);
    std::cout
        << "PASS recovery: clean boot, interrupted cycle, durable start gate, failed write, full cooling\n";
}
