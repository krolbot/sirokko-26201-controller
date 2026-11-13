#include "control_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace sirokko;
using namespace control_fixture;

int main() {
    auto no_range = envelope();
    no_range.minimum_speed_code = 0;
    assert(!valid_safety_envelope(no_range));

    auto controller = ignition();
    Settings settings = controller.settings();
    settings.room_setpoint_milli_c = 23000;
    assert(controller.update_settings(settings));
    uint64_t now = kIgnitionAt;
    auto tick = [&](int32_t room) {
        now += 100;
        auto input_value = input(now, 100000, 60000);
        input_value.sensors.room.milli_c = room;
        return controller.tick(input_value);
    };
    while (now < kRunAt - 100)
        tick(20000);
    auto state = tick(20000);
    assert(state.state == State::RUN);
    const auto run = state.run_id;
    auto hold_room = [&](int32_t room) {
        for (unsigned sample = 0; sample < 9000; ++sample) {
            state = tick(room);
            assert(state.state == State::RUN && !state.faults);
            assert(state.outputs.fuel && state.outputs.fan_request && !state.outputs.glow);
            assert(state.outputs.speed_code >= envelope().minimum_speed_code);
            assert(state.outputs.speed_code <= envelope().maximum_speed_code);
            assert(state.run_id == run);
        }
        return state.outputs.speed_code;
    };
    const auto hot_command = hold_room(30000);
    const auto cold_command = hold_room(10000);
    assert(cold_command > hot_command + 200);
    const auto warm_command = hold_room(30000);
    assert(warm_command + 200 < cold_command);

    // Profile and settings suffice after restart; no learned state is required.
    Controller restored(envelope(), settings, 11, Startup::CLEAN);
    assert(restored.snapshot(0).state == State::IDLE);
    assert(!restored.snapshot(0).outputs.fuel);
    auto start = input(100);
    start.start = true;
    assert(restored.tick(start).state == State::PREHEAT);

    // The configured upper bound is shared by ignition, display and auto/manual transfer.
    auto narrowed = envelope();
    narrowed.maximum_speed_code = 1500;
    Settings stored{};
    stored.manual_percent = 60;
    Controller narrowed_control(narrowed, stored, 12, Startup::CLEAN);
    assert(narrowed_control.settings().manual_percent == 80);
    auto begin = input(120);
    begin.start = true;
    assert(narrowed_control.tick(begin).state == State::PREHEAT);
    narrowed_control.tick(input(kIgnitionAt));
    auto active = confirm_ignition(narrowed_control);
    assert(active.outputs.speed_code == 1500 && active.command_percent == 100.0);
    auto manual = narrowed_control.settings();
    manual.mode = Mode::MANUAL;
    assert(narrowed_control.update_settings(manual));
    assert(narrowed_control.settings().manual_percent == 100);
    assert(narrowed_control.tick(input(kRunAt + 100, 50000)).outputs.speed_code == 1500);

    // A fixed command interval is valid and never divides by zero or fabricates modulation.
    auto fixed = envelope();
    fixed.minimum_speed_code = fixed.maximum_speed_code;
    Controller fixed_control(fixed, {}, 13, Startup::CLEAN);
    assert(fixed_control.settings().manual_percent == 100);
    assert(fixed_control.tick(begin).state == State::PREHEAT);
    fixed_control.tick(input(kIgnitionAt));
    confirm_ignition(fixed_control);
    for (uint64_t t = kRunAt + 100; t <= kRunAt + 60000; t += 100) {
        auto sample = input(t, 50000);
        sample.sensors.room.milli_c = 30000;
        const auto observed = fixed_control.tick(sample);
        assert(observed.state == State::RUN && !observed.faults);
        assert(observed.outputs.speed_code == fixed.maximum_speed_code && observed.room_excess);
    }

    // Explicit STOP and a thermocouple fault still override comfort control.
    auto faulted = controller;
    auto failure = input(now + 100, 100000, 60000);
    failure.sensors.exhaust.valid = false;
    auto fault = faulted.tick(failure);
    assert(fault.faults & FAULT_EXHAUST);
    off(fault);
    auto stop = input(now + 100, 100000, 60000);
    stop.stop = true;
    state = controller.tick(stop);
    assert(state.state == State::STOP_PURGE && state.outputs.fan_request);
    off(state);
    std::cout << "PASS thermostat: colder increases command, warmer reduces it without extinguishing, "
                 "range bounds, profile-only restart, fixed command, STOP and sensor fault\n";
}
