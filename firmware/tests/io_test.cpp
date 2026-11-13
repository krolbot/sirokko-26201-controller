#include "sirokko/control.hpp"
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
    web_protocol::Command command;
    const char *request = R"({"action":"START","command_id":"1"})";
    assert(web_protocol::parse_challenge_request(request, std::strlen(request), &command) ==
           web_protocol::ParseError::OK);
    assert(web_protocol::parse_command_request(request, std::strlen(request), &command) !=
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
