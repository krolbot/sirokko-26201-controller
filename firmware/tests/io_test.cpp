#include "sirokko/control.hpp"
#include "control_fixture.hpp"
#include "start_stop_button.hpp"
#include "web_protocol.hpp"
#include "bounded_receive.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace sirokko;
struct Pins {
    bool fuel{}, glow{}, fan{}, fail_pwm{}, fail_fuel{};
    uint16_t pwm{};
};
bool digital(void *ctx, OutputLine line, bool on) {
    auto &p = *static_cast<Pins *>(ctx);
    if (line == OutputLine::FUEL) {
        if (on && p.fail_fuel)
            return false;
        p.fuel = on;
    }
    if (line == OutputLine::GLOW)
        p.glow = on;
    if (line == OutputLine::FAN_REQUEST)
        p.fan = on;
    return true;
}
bool pwm(void *ctx, uint16_t speed) {
    auto &p = *static_cast<Pins *>(ctx);
    if (p.fail_pwm)
        return false;
    p.pwm = speed;
    return true;
}
struct Local {
    start_stop_button::Snapshot view{State::IDLE, Block::NONE, 1};
    unsigned starts{}, stops{};
};
bool read(void *ctx, start_stop_button::Snapshot *s) {
    *s = static_cast<Local *>(ctx)->view;
    return true;
}
bool start(void *ctx, uint64_t version) {
    auto &l = *static_cast<Local *>(ctx);
    assert(version == 1);
    ++l.starts;
    return true;
}
bool stop(void *ctx) {
    ++static_cast<Local *>(ctx)->stops;
    return true;
}
struct Transfer {
    uint64_t time{};
    size_t consumed{};
    bool timeout{};
};
int receive(void *ctx, char *data, size_t n) {
    auto &t = *static_cast<Transfer *>(ctx);
    t.time += 10;
    if (t.timeout)
        return -1;
    std::memset(data, 1, n);
    return static_cast<int>(n);
}
bool consume(void *ctx, const char *, size_t n) {
    static_cast<Transfer *>(ctx)->consumed += n;
    return true;
}
uint64_t clock_ms(void *ctx) {
    return static_cast<Transfer *>(ctx)->time;
}
int main() {
    Pins pins;
    FailClosedOutputDriver outputs;
    assert(outputs.apply({false, 0, true, false}, &pins, digital, pwm));
    assert(pins.glow && !pins.fuel && !pins.fan && pins.pwm == 0);
    assert(outputs.apply({true, 1000, true, true}, &pins, digital, pwm));
    assert(pins.fuel && pins.glow && pins.fan);
    pins.fail_pwm = true;
    assert(!outputs.apply({true, 1000, true, true}, &pins, digital, pwm));
    assert(!pins.fuel && !pins.glow && pins.fan);
    pins.fail_pwm = false;
    pins.fail_fuel = true;
    assert(!outputs.apply({true, 1000, true, true}, &pins, digital, pwm));
    assert(!pins.fuel && !pins.glow);
    assert(!outputs.apply({false, 0, true, true}, &pins, digital, pwm));
    assert(!pins.fuel && !pins.glow);
    Local local;
    start_stop_button::Button button({&local, read, start, stop});
    button.reset(true, 0);
    for (unsigned t = 10; t <= 100; t += 10)
        button.poll(true, t);
    assert(local.starts == 0);
    for (unsigned t = 110; t <= 150; t += 10)
        button.poll(false, t);
    for (unsigned t = 160; t <= 250; t += 10)
        button.poll(true, t);
    assert(local.starts == 1);
    for (unsigned t = 260; t <= 300; t += 10)
        button.poll(false, t);
    local.view.state = State::RUN;
    for (unsigned t = 310; t <= 400; t += 10)
        button.poll(true, t);
    assert(local.stops == 1);
    // A trial consumes a live hold, never the normal one-shot START/STOP command.
    Local trial;
    trial.view.state = State::TRIAL_READY;
    trial.view.trial_active = true;
    start_stop_button::Button held({&trial, read, start, stop});
    held.reset(true, 0);
    for (unsigned t = 10; t <= 100; t += 10)
        held.poll(true, t);
    assert(!held.held_sample().valid);
    for (unsigned t = 110; t <= 150; t += 10)
        held.poll(false, t);
    assert(held.held_sample().valid && !held.held_sample().held);
    held.poll(true, 160);
    assert(!held.held_sample().held && !held.held_sample().valid);
    for (unsigned t = 170; t <= 200; t += 10)
        held.poll(true, t);
    assert(held.held_sample().valid && held.held_sample().held);
    assert(held.held_sample().sample_ms == 200 && !trial.starts && !trial.stops);
    held.poll(false, 210);
    assert(!held.held_sample().held && !held.held_sample().valid);
    for (unsigned t = 220; t <= 250; t += 10)
        held.poll(false, t);
    for (unsigned t = 260; t <= 300; t += 10)
        held.poll(true, t);
    assert(held.held_sample().valid && held.held_sample().held);
    held.poll(true, 410); // A stalled poll task cannot retain a usable hold.
    assert(!held.held_sample().valid);
    held.poll(true, 420);
    assert(!held.held_sample().valid);
    // Exercise the actual debouncer and controller together across trial admission.
    Controller probe({}, Settings{}, 1, Startup::CLEAN);
    Local rig;
    start_stop_button::Button physical({&rig, read, start, stop});
    physical.reset(false, 0);
    auto physical_tick = [&](uint64_t at, bool pressed, bool request = false) {
        const auto previous = probe.snapshot(at);
        rig.view = {previous.state, previous.start_block, previous.state_version, previous.trial_active};
        physical.poll(pressed, at);
        const auto key = physical.held_sample();
        auto input = control_fixture::input(at);
        input.trial_request = request;
        input.local_button_held = key.held;
        input.local_button_valid = key.valid;
        input.local_button_sample_ms = key.sample_ms;
        return probe.tick(input);
    };
    for (unsigned t = 10; t <= 40; t += 10)
        physical_tick(t, false);
    assert(physical_tick(50, true, true).state == State::UNCONFIGURED);
    for (unsigned t = 60; t <= 90; t += 10)
        physical_tick(t, true);
    assert(physical_tick(100, true, true).state == State::UNCONFIGURED);
    for (unsigned t = 110; t <= 150; t += 10)
        physical_tick(t, false);
    assert(physical_tick(160, false, true).state == State::TRIAL_READY);
    for (unsigned t = 170; t <= 190; t += 10)
        assert(physical_tick(t, true).state == State::TRIAL_READY);
    auto started = physical_tick(200, true);
    assert(started.state == State::PREHEAT && started.outputs.glow && !started.outputs.fuel);
    auto held_probe = probe;
    auto released = physical_tick(210, false);
    assert(released.state == State::STOP_PURGE && !released.faults);
    assert(!released.outputs.glow && !released.outputs.fuel && released.outputs.fan_request);
    assert(!rig.starts && !rig.stops);
    auto missed = control_fixture::input(200 + kLocalButtonFreshMs + 1);
    missed.local_button_valid = missed.local_button_held = true;
    missed.local_button_sample_ms = 200;
    const auto stopped = held_probe.tick(missed);
    assert(stopped.state == State::FAULT_PURGE && (stopped.faults & FAULT_LOCAL_CONTROLS));
    assert(!stopped.outputs.fuel && !stopped.outputs.glow);
    web_protocol::Command command;
    const char *request = R"({"action":"START","command_id":"1"})";
    assert(web_protocol::parse_challenge_request(request, std::strlen(request), &command) ==
           web_protocol::ParseError::OK);
    assert(web_protocol::parse_command_request(request, std::strlen(request), &command) !=
           web_protocol::ParseError::OK);
    web_protocol::Command trial_command{};
    const char *first_start = R"({"action":"TRIAL_START","command_id":"2"})";
    assert(web_protocol::parse_challenge_request(first_start, std::strlen(first_start), &trial_command) ==
           web_protocol::ParseError::OK);
    assert(trial_command.action == Action::TRIAL_START);
    const char *extend_trial = R"({"action":"TRIAL_START","command_id":"2","delay_ms":999999})";
    assert(web_protocol::parse_challenge_request(extend_trial, std::strlen(extend_trial), &trial_command) ==
           web_protocol::ParseError::UNKNOWN_FIELD);
    const char *confirm_trial = R"({"action":"TRIAL_CONFIRM","command_id":"3"})";
    assert(web_protocol::parse_challenge_request(confirm_trial, std::strlen(confirm_trial), &trial_command) ==
           web_protocol::ParseError::OK);
    assert(trial_command.action == Action::TRIAL_CONFIRM);
    const char *pwm_trial = R"({"action":"TRIAL_PWM","command_id":"4","speed_code":2007})";
    assert(web_protocol::parse_challenge_request(pwm_trial, std::strlen(pwm_trial), &trial_command) ==
           web_protocol::ParseError::OK);
    assert(trial_command.action == Action::TRIAL_PWM && trial_command.speed_code == 2007);
    const auto original_digest = trial_command.payload_digest;
    ++trial_command.speed_code;
    assert(!web_protocol::digest_equal(original_digest, web_protocol::compute_payload_digest(trial_command)));
    for (const char *bad : {R"({"action":"TRIAL_PWM","command_id":"4"})",
                            R"({"action":"TRIAL_PWM","command_id":"4","speed_code":0})",
                            R"({"action":"TRIAL_PWM","command_id":"4","speed_code":4015})",
                            R"({"action":"TRIAL_PWM","command_id":"4","speed_code":65537})",
                            R"({"action":"TRIAL_PWM","command_id":"4","speed_code":1.5})",
                            R"({"action":"TRIAL_PWM","command_id":"4","speed_code":2007,"delay_ms":900000})",
                            R"({"action":"TRIAL_CONFIRM","command_id":"4","speed_code":2007})"})
        assert(web_protocol::parse_challenge_request(bad, std::strlen(bad), &trial_command) !=
               web_protocol::ParseError::OK);
    const char *duplicate = R"({"action":"START","action":"CALIBRATE","command_id":"1"})";
    assert(web_protocol::parse_challenge_request(duplicate, std::strlen(duplicate), &command) ==
           web_protocol::ParseError::DUPLICATE_FIELD);
    const char *arm = R"({"action":"ARM","command_id":"1","delay_ms":1000})";
    assert(web_protocol::parse_challenge_request(arm, std::strlen(arm), &command) ==
           web_protocol::ParseError::ACTION);
    const char *partial =
        R"({"action":"SETTINGS","command_id":"1","settings":{"room_setpoint_milli_c":23000}})";
    assert(web_protocol::parse_challenge_request(partial, std::strlen(partial), &command) ==
           web_protocol::ParseError::MISSING_FIELD);
    const char *full =
        R"({"action":"SETTINGS","command_id":"1","settings":{"room_setpoint_milli_c":23000,"duration_ms":60000,"mode":"MANUAL","manual_percent":75,"slew_up_per_s":0.002,"slew_down_per_s":0.003}})";
    assert(web_protocol::parse_challenge_request(full, std::strlen(full), &command) ==
           web_protocol::ParseError::OK);
    assert(valid_settings(command.settings) && command.settings.manual_percent == 75);
    char buffer[8];
    Transfer t;
    assert(receive_exact_bounded(20, 100, -1, buffer, sizeof(buffer), &t, receive, &t, consume, &t,
                                 clock_ms) == BoundedReceiveResult::COMPLETE &&
           t.consumed == 20);
    t = {0, 0, true};
    assert(receive_exact_bounded(20, 25, -1, buffer, sizeof(buffer), &t, receive, &t, consume, &t,
                                 clock_ms) == BoundedReceiveResult::DEADLINE_EXCEEDED &&
           t.consumed == 0);
    t = {};
    assert(receive_exact_bounded(20, 10, -1, buffer, sizeof(buffer), &t, receive, &t, consume, &t,
                                 clock_ms) == BoundedReceiveResult::DEADLINE_EXCEEDED &&
           t.consumed == 0);
    std::cout << "PASS IO/protocol: output failures, button debounce, strict JSON, receive deadlines\n";
}
