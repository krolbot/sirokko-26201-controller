#include "sirokko/control.hpp"

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace sirokko;

namespace {

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream(line);
    std::string field;
    while (std::getline(stream, field, ',')) fields.push_back(field);
    return fields;
}

uint8_t hex_byte(const std::string& value) {
    return static_cast<uint8_t>(std::stoul(value, nullptr, 16));
}

Sensors good(uint64_t now, int32_t exhaust = 100000, int32_t room = 15000) {
    return {{exhaust, now, 0, true}, {80000, now, 0, true}, {room, now, 0, true}};
}

ProcessProfile profile() {
    ProcessProfile value{};
    value.qualified = true;
    value.recovery_purge_ms = 100;
    value.prepurge_ms = 10;
    value.preheat_ms = 10;
    value.ignition_ms = 10;
    value.verify_ms = 10;
    value.postpurge_ms = 20;
    value.maximum_purge_exit_exhaust_milli_c = 150000;
    value.maximum_purge_exit_air_milli_c = 120000;
    value.maximum_exhaust_milli_c = 500000;
    value.maximum_air_milli_c = 400000;
    value.minimum_ignition_rise_milli_c = 20000;
    value.minimum_run_exhaust_milli_c = 110000;
    value.purge_speed_code = 2000;
    value.run_speed_code = 3000;
    return value;
}

void test_temperature_vectors(const char* path) {
    std::ifstream input(path);
    assert(input.good());
    std::string line;
    std::getline(input, line);
    size_t cases = 0;
    while (std::getline(input, line)) {
        const auto f = split(line);
        assert(f.size() == 11);
        const int32_t actual = decode_max31856_signed19(hex_byte(f[2]), hex_byte(f[3]), hex_byte(f[4]));
        assert(actual == std::stoi(f[6]));
        const bool below = actual < 0;
        const bool ge600 = actual >= 600 * 128;
        assert(below == (f[8] == "1"));
        assert(ge600 == (f[9] == "1"));
        ++cases;
    }
    assert(cases == 352);
}

void test_ds18b20(const char* path) {
    std::ifstream input(path);
    assert(input.good());
    std::string line;
    std::getline(input, line);
    size_t cases = 0;
    size_t usable = 0;
    while (std::getline(input, line)) {
        const auto f = split(line);
        assert(f.size() == 10 && f[1].size() == 18);
        uint8_t frame[9]{};
        for (size_t i = 0; i < 9; ++i) frame[i] = hex_byte(f[1].substr(i * 2, 2));
        const bool completed = f[2] == "1";
        const unsigned requested_resolution = std::stoul(f[3]);
        const bool expected_crc = f[4] == "1";
        const bool expected_usable = f[8] == "1";
        assert((onewire_crc8(frame, 8) == frame[8]) == expected_crc);
        int32_t value{};
        const bool actual = decode_ds18b20_scratchpad(frame, requested_resolution, completed, &value);
        assert(actual == expected_usable);
        if (actual) {
            assert(!f[7].empty());
            assert(value == static_cast<int32_t>(std::stod(f[7]) * 1000.0));
            ++usable;
        }
        ++cases;
    }
    assert(cases == 31 && usable == 25);
}

void test_gate() {
    CommandGate gate;
    const auto grant = gate.issue(Action::START, 42, 1000, 7, 99);
    assert(grant.valid);
    assert(!gate.consume(grant, 1001, 8));
    assert(gate.consume(grant, 1001, 7));
    assert(!gate.consume(grant, 1002, 7));
    const auto expired = gate.issue(Action::ARM, 5, 2000, 9, 100);
    assert(!gate.consume(expired, 7000, 9));
    const auto latest = gate.issue(Action::START, 6, 8000, 10, 101);
    assert(gate.consume(latest, 12999, 10));
    const auto boundary = gate.issue(Action::START, 7, 13000, 11, 102);
    assert(!gate.consume(boundary, 18000, 11));
}

void test_safe_closed() {
    ProcessProfile unqualified{};
    Controller machine(unqualified, {}, 1);
    TickInput in{};
    in.now_ms = 100;
    in.sensors = good(100);
    in.start = true;
    const auto state = machine.tick(in);
    assert(state.state == State::RECOVERY_PURGE);
    assert(state.start_block == Block::PROFILE_NOT_QUALIFIED);
    assert(state.outputs.fan_request && state.outputs.speed_code == kPwmMaximum);
    assert(!state.outputs.glow && !state.outputs.fuel);
}

void test_state_machine_and_priority() {
    Settings settings{};
    settings.duration_ms = 1000;
    Controller machine(profile(), settings, 123);
    TickInput in{};
    in.sensors = good(0);
    assert(machine.tick(in).state == State::RECOVERY_PURGE);
    in.now_ms = 100;
    in.sensors = good(100);
    assert(machine.tick(in).state == State::IDLE);

    in.now_ms = 101;
    in.sensors = good(101);
    in.start = true;
    auto s = machine.tick(in);
    assert(s.state == State::PREPURGE && s.run_id != 0 && s.duration_active);

    in.start = false;
    in.now_ms = 111;
    in.sensors = good(111);
    assert(machine.tick(in).state == State::PREHEAT);
    in.now_ms = 121;
    in.sensors = good(121);
    assert(machine.tick(in).state == State::IGNITION);
    in.now_ms = 131;
    in.sensors = good(131, 130000);
    s = machine.tick(in);
    assert(s.state == State::VERIFY && s.outputs.fuel && !s.outputs.glow);
    in.now_ms = 141;
    in.sensors = good(141, 130000);
    s = machine.tick(in);
    assert(s.state == State::RUN && s.outputs.fuel && !s.outputs.glow);

    in.now_ms = 142;
    in.sensors = good(142, 130000);
    in.stop = true;
    in.start = true;
    s = machine.tick(in);
    assert(s.state == State::STOP_PURGE && !s.outputs.fuel && !s.outputs.glow);
    in.stop = in.start = false;
    in.now_ms = 162;
    in.sensors = good(162, 130000);
    assert(machine.tick(in).state == State::IDLE);
}

void test_delayed_stop_and_fault_barriers() {
    Controller machine(profile(), {}, 9);
    TickInput in{};
    in.sensors = good(100);
    in.now_ms = 100;
    assert(machine.tick(in).state == State::IDLE);
    in.arm_delayed = true;
    in.delay_ms = 100;
    in.now_ms = 101;
    in.sensors = good(101);
    auto s = machine.tick(in);
    assert(s.delayed_armed);

    in.arm_delayed = false;
    in.stop = true;
    in.now_ms = 201;
    in.sensors = good(201);
    s = machine.tick(in);
    assert(s.state == State::IDLE && !s.delayed_armed && s.run_id == 0);

    in.stop = false;
    in.start = true;
    in.now_ms = 202;
    in.sensors = good(202);
    assert(machine.tick(in).state == State::PREPURGE);
    in.start = false;
    in.now_ms = 203;
    in.sensors = good(203);
    in.sensors.exhaust.valid = false;
    s = machine.tick(in);
    assert(s.state == State::FAULT_PURGE && !s.outputs.fuel && !s.outputs.glow);
    in.now_ms = 223;
    in.sensors = good(223);
    assert(machine.tick(in).state == State::FAULT);
    in.reset_fault = true;
    in.start = true;
    in.now_ms = 224;
    in.sensors = good(224);
    s = machine.tick(in);
    assert(s.state == State::IDLE && s.run_id == 0);

    in.reset_fault = in.start = false;
    in.stop = in.arm_delayed = true;
    in.delay_ms = 100;
    in.now_ms = 225;
    in.sensors = good(225);
    s = machine.tick(in);
    assert(s.state == State::IDLE && !s.delayed_armed);
}

void test_duration_exact_boundary() {
    Settings settings{};
    settings.duration_ms = 100;
    Controller machine(profile(), settings, 3);
    TickInput in{};
    in.now_ms = 100;
    in.sensors = good(100);
    machine.tick(in);
    in.now_ms = 101;
    in.sensors = good(101);
    in.start = true;
    machine.tick(in);
    in.start = false;
    in.now_ms = 200;
    in.sensors = good(200, 130000);
    assert(machine.tick(in).duration_remaining_ms == 1);
    in.now_ms = 201;
    in.sensors = good(201, 130000);
    assert(machine.tick(in).state == State::STOP_PURGE);
}

void test_output_driver_fault_is_safe() {
    Controller machine(profile(), {}, 4);
    TickInput in{};
    in.now_ms = 100;
    in.sensors = good(100);
    assert(machine.tick(in).state == State::IDLE);
    in.now_ms = 101;
    in.external_faults = FAULT_CONTROL_WATCHDOG;
    const auto state = machine.tick(in);
    assert(state.state == State::FAULT_PURGE);
    assert((state.faults & FAULT_CONTROL_WATCHDOG) != 0);
    assert(!state.outputs.glow && !state.outputs.fuel);
}

void test_verify_failure_and_purge_conditions() {
    Controller machine(profile(), {}, 5);
    TickInput in{};
    in.now_ms = 100;
    in.sensors = good(100);
    assert(machine.tick(in).state == State::IDLE);
    in.start = true;
    in.now_ms = 101;
    in.sensors = good(101);
    machine.tick(in);
    in.start = false;
    in.now_ms = 111;
    in.sensors = good(111);
    machine.tick(in);
    in.now_ms = 121;
    in.sensors = good(121);
    machine.tick(in);
    in.now_ms = 131;
    in.sensors = good(131, 130000);
    auto state = machine.tick(in);
    assert(state.state == State::VERIFY && !state.outputs.glow && state.outputs.fuel);
    in.now_ms = 141;
    in.sensors = good(141, 100000);
    state = machine.tick(in);
    assert(state.state == State::FAULT_PURGE && !state.outputs.glow && !state.outputs.fuel);
    const uint64_t purge_id = state.purge_id;
    in.stop = true;
    in.now_ms = 170;
    in.sensors = good(170, 200000);
    state = machine.tick(in);
    assert(state.state == State::FAULT_PURGE && state.purge_id == purge_id &&
           !state.purge_temperature_ready);
    in.stop = false;
    in.external_faults = FAULT_CONTROL_WATCHDOG;
    in.now_ms = 171;
    in.sensors = good(171, 200000);
    state = machine.tick(in);
    assert(state.state == State::FAULT_PURGE && state.purge_id == purge_id &&
           state.purge_minimum_remaining_ms == 0);
    in.external_faults = 0;
    in.now_ms = 172;
    in.sensors = good(172);
    in.sensors.exhaust.valid = false;
    state = machine.tick(in);
    assert(state.state == State::FAULT_PURGE && state.purge_id == purge_id &&
           !state.purge_sensors_fresh);
    in.now_ms = 173;
    in.sensors = good(173);
    assert(machine.tick(in).state == State::FAULT);

    Controller recovery(profile(), {}, 6);
    in = {};
    in.now_ms = 50;
    in.sensors = good(50);
    in.stop = true;
    const auto before = recovery.tick(in);
    assert(before.state == State::RECOVERY_PURGE && before.purge_id != 0);
    in.now_ms = 60;
    in.sensors = good(60);
    const auto after = recovery.tick(in);
    assert(after.state == State::RECOVERY_PURGE && after.purge_id == before.purge_id);
}

struct OutputLog {
    std::vector<std::string> events;
    bool fail_pwm{};
};

bool fake_digital(void* context, OutputLine line, bool enabled) {
    auto& log = *static_cast<OutputLog*>(context);
    const char* name = line == OutputLine::FUEL ? "FUEL" : line == OutputLine::GLOW ? "GLOW" : "FAN";
    log.events.emplace_back(std::string(name) + (enabled ? "=1" : "=0"));
    return true;
}

bool fake_pwm(void* context, uint16_t speed_code) {
    auto& log = *static_cast<OutputLog*>(context);
    log.events.emplace_back("PWM=" + std::to_string(speed_code));
    return !log.fail_pwm;
}

void test_output_driver_fail_closed_order() {
    FailClosedOutputDriver driver;
    OutputLog log{};
    log.fail_pwm = true;
    assert(!driver.apply({true, 2000, true, true}, &log, fake_digital, fake_pwm));
    for (const auto& event : log.events) assert(event != "GLOW=1" && event != "FUEL=1");
    assert(!driver.current().glow && !driver.current().fuel);

    log = {};
    assert(driver.apply({true, 2000, true, true}, &log, fake_digital, fake_pwm));
    const auto fan = std::find(log.events.begin(), log.events.end(), "FAN=1");
    const auto pwm = std::find(log.events.begin(), log.events.end(), "PWM=2000");
    const auto glow = std::find(log.events.begin(), log.events.end(), "GLOW=1");
    const auto fuel = std::find(log.events.begin(), log.events.end(), "FUEL=1");
    assert(fan < pwm && pwm < glow && glow < fuel);
}

void test_max31856_deadline_boundaries() {
    assert(max31856_sample_within_deadline(1000, 1210, 1210));
    assert(!max31856_sample_within_deadline(1000, 1211, 1211));
    assert(!max31856_sample_within_deadline(1000, 1209, 1211));
    assert(!max31856_sample_within_deadline(1000, 999, 1000));
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 3);
    test_temperature_vectors(argv[1]);
    test_ds18b20(argv[2]);
    test_gate();
    test_safe_closed();
    test_state_machine_and_priority();
    test_delayed_stop_and_fault_barriers();
    test_duration_exact_boundary();
    test_output_driver_fault_is_safe();
    test_verify_failure_and_purge_conditions();
    test_output_driver_fail_closed_order();
    test_max31856_deadline_boundaries();
    std::cout << "PASS: 352 MAX31856 vectors + deadline edges; 31 DS18B20 vectors (25 accepted/6 rejected); "
                 "command freshness; state/timer/fault/STOP/purge barriers; fail-closed output order\n";
}
