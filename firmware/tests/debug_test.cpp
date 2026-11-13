#include "control_fixture.hpp"
#include "command_policy.hpp"
#include "web_protocol.hpp"
#include "start_stop_button.hpp"
#include <cstring>
#include <iostream>
using namespace sirokko;
using namespace control_fixture;

static Snapshot command(Controller &c, uint64_t now, Outputs outputs, bool durable = true) {
    auto i = input(now);
    i.set_debug_outputs = true;
    i.debug_outputs = outputs;
    i.cycle_record_ready = durable;
    return c.tick(i);
}
static void enter(Controller &c, uint64_t now = 100) {
    auto i = input(now);
    i.enter_debug = true;
    assert(c.tick(i).state == State::DEBUG);
}
int main() {
    Controller motor({}, {}, 1, Startup::CLEAN);
    enter(motor);
    auto s = command(motor, 120, {true, 800, false, false});
    assert(s.outputs.speed_code == 800 && s.state == State::DEBUG);
    s = command(motor, 140, {true, 800, true, true});
    off(s); // Unset thermal limits do not prevent motor testing, but cannot enable heat.
    assert(!motor.update_settings(Settings{}));
    s = motor.tick(input(5120));
    assert(s.state == State::UNCONFIGURED && !s.outputs.fan_request);
    s = command(motor, 5140, {true, 800, false, false});
    assert(s.state == State::UNCONFIGURED && !s.outputs.fan_request); // No late revival.

    auto partial = SafetyEnvelope{};
    partial.maximum_exhaust_milli_c = 300000;
    partial.maximum_air_milli_c = 150000;
    assert(valid_profile_draft(partial) && debug_heat_configured(partial));
    assert(!valid_safety_envelope(partial));
    Controller c({}, {}, 2, Startup::CLEAN);
    assert(c.install_safety_envelope(partial, false, 0));
    assert(c.snapshot(0).state == State::UNCONFIGURED);
    enter(c);
    s = command(c, 120, {false, 0, true, false}, false);
    off(s); // Write-before-heat is mandatory, even in manual tests.
    s = command(c, 140, {false, 0, true, false});
    assert(s.outputs.glow && !s.outputs.fuel);
    auto stop = input(160);
    stop.stop = true;
    stop.set_debug_outputs = true;
    stop.debug_outputs = {true, 1200, true, true};
    s = c.tick(stop);
    assert(s.state == State::STOP_PURGE && s.outputs.fan_request);
    off(s);
    assert(!c.install_safety_envelope(partial, false, 180));
    s = c.tick(input(150160, 40000));
    assert(s.state == State::STOP_PURGE); // "Below", not "at or below".
    s = c.tick(input(150180, 39999));
    assert(s.state == State::UNCONFIGURED && !s.outputs.fan_request);

    Controller reboot(partial, {}, 3, Startup::RECOVER);
    s = reboot.tick(input(0, 70000));
    assert(s.state == State::RECOVERY_PURGE && s.outputs.fan_request);
    off(s);
    s = reboot.tick(input(150000, 30000));
    assert(s.state == State::UNCONFIGURED);

    Controller thermal(partial, {}, 4, Startup::CLEAN);
    enter(thermal);
    command(thermal, 120, {true, 1200, false, true});
    auto hot = input(140, 300000);
    s = thermal.tick(hot);
    assert(s.faults & FAULT_OVER_TEMPERATURE);
    off(s);
    assert(s.outputs.fan_request);

    Controller lost(partial, {}, 5, Startup::CLEAN);
    enter(lost);
    command(lost, 120, {true, 1200, false, true});
    auto bad = input(140);
    bad.sensors.air.valid = false;
    s = lost.tick(bad);
    assert(s.faults & FAULT_AIR);
    off(s);
    assert(lost.tick(bad).outputs.fan_request);

    Controller bounded(partial, {}, 6, Startup::CLEAN);
    enter(bounded);
    command(bounded, 120, {true, 1200, true, true});
    for (uint64_t now = 1120; now <= 90120; now += 1000) {
        auto previous = bounded.snapshot(now - 1000).outputs;
        s = command(bounded, now, previous);
        if (now >= 60120)
            assert(!s.outputs.glow);
    }
    assert(s.faults & FAULT_IGNITION_TIMEOUT);
    off(s);

    Controller link(partial, {}, 7, Startup::CLEAN);
    enter(link);
    command(link, 120, {true, 1200, false, true});
    s = link.tick(input(5120, 100000));
    assert(s.state == State::STOP_PURGE && s.outputs.fan_request);
    off(s);

    RuntimeView view{};
    view.startup_ready = view.local_controls_initialized = true;
    view.control.state = State::DEBUG;
    assert(command_policy::block(view, Action::DEBUG_SET) == Block::NONE);
    for (auto action : {Action::DEBUG_ENTER, Action::START, Action::CALIBRATE, Action::SETTINGS,
                        Action::TIMER, Action::PROFILE, Action::OTA})
        assert(command_policy::block(view, action) != Block::NONE);
    assert(command_policy::wifi_password_block(view) != Block::NONE);

    using namespace web_protocol;
    Command parsed{};
    const char *json = R"({"action":"DEBUG_SET","command_id":"1","speed_code":800,"glow":false,"fuel":true})";
    assert(parse_challenge_request(json, std::strlen(json), &parsed) == ParseError::OK);
    const auto original = parsed.payload_digest;
    parsed.debug_outputs.fuel = false;
    assert(!digest_equal(original, compute_payload_digest(parsed)));
    const char *invalid =
        R"({"action":"DEBUG_SET","command_id":"1","speed_code":0,"glow":false,"fuel":true})";
    assert(parse_challenge_request(invalid, std::strlen(invalid), &parsed) == ParseError::FIELD_VALUE);
    std::cout
        << "PASS debug: partial setup, motor-only, durable heat admission, STOP, watchdog, cooling "
           "threshold, recovery, sensor/overheat faults, timed glow/fuel, command isolation and digest\n";
}
