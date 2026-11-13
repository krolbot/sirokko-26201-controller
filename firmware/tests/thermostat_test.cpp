#include "control_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace sirokko;
using namespace control_fixture;

int main() {
    // A model must provide a real interval, not a single accepted command.
    auto one_point = model();
    one_point.minimum_speed_code = one_point.maximum_speed_code;
    one_point.fingerprint = learned_model_fingerprint(one_point);
    Controller rejected(envelope(), Settings{}, 10, Startup::CLEAN);
    assert(!rejected.install_learned_model(one_point));
    auto no_range = envelope();
    no_range.exploration_floor_code = 0;
    assert(commissioning_configuration_block(no_range, Settings{}) != Block::NONE);

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
    const auto fingerprint = state.learned_model.fingerprint;
    const auto run = state.run_id;
    auto hold_room = [&](int32_t room) {
        for (unsigned sample = 0; sample < 9000; ++sample) {
            state = tick(room);
            assert(state.state == State::RUN && !state.faults);
            assert(state.outputs.fuel && state.outputs.fan_request && !state.outputs.glow);
            assert(state.outputs.speed_code >= model().minimum_speed_code);
            assert(state.outputs.speed_code <= model().maximum_speed_code);
            assert(!state.commissioning.active && state.run_id == run);
            assert(state.learned_model.fingerprint == fingerprint);
        }
        return state.outputs.speed_code;
    };
    const auto hot_command = hold_room(30000);
    const auto cold_command = hold_room(10000);
    assert(cold_command > hot_command + 200);
    const auto warm_command = hold_room(30000);
    assert(warm_command + 200 < cold_command);

    // The saved model retains temperature regulation after a controller restart.
    Controller restored(envelope(), settings, 11, Startup::CLEAN);
    assert(restored.install_learned_model(state.learned_model));
    assert(restored.snapshot(0).state == State::IDLE);
    assert(!restored.snapshot(0).outputs.fuel);
    auto start = input(100);
    start.start = true;
    assert(restored.tick(start).state == State::PREHEAT);
    assert(!restored.snapshot(100).commissioning.active);

    // A narrowed model uses its own maximum for display and auto/manual transfer.
    auto narrowed = model();
    narrowed.maximum_speed_code = 1500;
    narrowed.fingerprint = learned_model_fingerprint(narrowed);
    for (bool recalibrate : {false, true}) {
        Settings stored{};
        stored.manual_percent = 60;
        Controller narrowed_control(envelope(), stored, 12, Startup::CLEAN);
        assert(narrowed_control.install_learned_model(narrowed));
        assert(narrowed_control.settings().manual_percent == 80);
        auto begin = input(120);
        begin.start = !recalibrate;
        begin.start_commissioning = recalibrate;
        assert(narrowed_control.tick(begin).state == State::PREHEAT);
        narrowed_control.tick(input(kIgnitionAt));
        auto active = confirm_ignition(narrowed_control);
        assert(active.outputs.speed_code == (recalibrate ? envelope().reference_speed_code : 1500));
        assert(active.command_percent == 100.0);
        if (!recalibrate) {
            auto manual = narrowed_control.settings();
            manual.mode = Mode::MANUAL;
            assert(narrowed_control.update_settings(manual));
            assert(narrowed_control.settings().manual_percent == 100);
            assert(narrowed_control.tick(input(kRunAt + 100, 50000)).outputs.speed_code == 1500);
        } else {
            active = narrowed_control.tick(input(kRunAt + 100, 50000));
            assert(active.commissioning.active && !active.faults);
        }
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
                 "range bounds, saved model, no recalibration, STOP and sensor fault\n";
}
