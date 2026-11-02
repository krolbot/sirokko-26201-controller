#include "sirokko/control.hpp"

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
    return {true, 100, 10, 10, 10, 10, 20, 500000, 400000, 20000, 110000, 2000, 3000};
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

void test_ds18b20() {
    uint8_t valid_zero[9]{0x00, 0x00, 0x4b, 0x46, 0x7f, 0xff, 0x0c, 0x10, 0xc8};
    int32_t value = 1;
    assert(decode_ds18b20_scratchpad(valid_zero, &value) && value == 0);

    uint8_t invalid_config[9]{0x90, 0x01, 0x4b, 0x46, 0xff, 0xff, 0x0c, 0x10, 0xea};
    assert(onewire_crc8(invalid_config, 8) == invalid_config[8]);
    assert(!decode_ds18b20_scratchpad(invalid_config, &value));

    uint8_t zeros[9]{};
    assert(onewire_crc8(zeros, 8) == 0);
    assert(!decode_ds18b20_scratchpad(zeros, &value));

    valid_zero[8] ^= 1;
    assert(!decode_ds18b20_scratchpad(valid_zero, &value));
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
    assert(machine.tick(in).state == State::VERIFY);
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

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    test_temperature_vectors(argv[1]);
    test_ds18b20();
    test_gate();
    test_safe_closed();
    test_state_machine_and_priority();
    test_delayed_stop_and_fault_barriers();
    test_duration_exact_boundary();
    std::cout << "PASS: 352 MAX31856 vectors; DS18B20 format/CRC; command freshness; state/timer/fault/STOP barriers\n";
}
