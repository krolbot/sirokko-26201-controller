#include "control_fixture.hpp"
#include "command_policy.hpp"
#include "envelope_fields.hpp"
#include "web_protocol.hpp"
#include "web_state.hpp"
#include <cstring>
#include <iostream>
#include <string>
using namespace sirokko;
using namespace control_fixture;
using namespace sirokko::web_protocol;

std::string request(const SafetyEnvelope &p, const char *omit = "", const char *extra = "") {
    std::string result = R"({"action":"PROFILE","command_id":"1","envelope":{)";
    bool first = true;
    envelope_fields(p, [&](const char *name, auto value) {
        if (!std::strcmp(name, omit))
            return;
        if (!first)
            result += ',';
        first = false;
        result += '"' + std::string(name) + "\":" + std::to_string(value);
    });
    return result + extra + "}}";
}
ParseError parse(const std::string &text, Command *command) {
    return parse_challenge_request(text.data(), text.size(), command);
}

int main() {
    const auto profile = envelope(); // Synthetic fixture, never a heater preset.
    Command command{};
    const auto body = request(profile);
    assert(parse(body, &command) == ParseError::OK);
    assert(command.action == Action::PROFILE && command.envelope.provisioned);
    assert(envelope_fingerprint(command.envelope) == envelope_fingerprint(profile));
    assert(parse_command_request(body.data(), body.size(), &command) == ParseError::MISSING_FIELD);
    assert(parse(body, &command) == ParseError::OK);
    const auto original = command;
    envelope_fields(profile, [&](const char *name, auto) {
        assert(parse(request(profile, name), &command) == ParseError::MISSING_FIELD);
        const std::string duplicate = ",\"" + std::string(name) + "\":1";
        assert(parse(request(profile, "", duplicate.c_str()), &command) == ParseError::DUPLICATE_FIELD);
        auto changed = profile;
        envelope_fields(changed, [&](const char *key, auto &value) {
            if (!std::strcmp(name, key))
                ++value;
        });
        assert(parse(request(changed), &command) == ParseError::OK);
        assert(!digest_equal(original.payload_digest, command.payload_digest));
        ChallengeBinding binding;
        command.has_grant = true;
        command.grant = {1, 2, 50};
        binding.bind(Action::PROFILE, command.grant, original.payload_digest);
        assert(binding.consume(command, 2, 2) == BindingResult::PAYLOAD);
    });
    assert(parse(request(profile, "", ",\"provisioned\":true"), &command) == ParseError::UNKNOWN_FIELD);
    for (const char *value : {"-1", "1.5", "65536", "null", "true", "\"2000\""}) {
        const std::string invalid = ",\"reference_speed_code\":" + std::string(value);
        assert(parse(request(profile, "reference_speed_code", invalid.c_str()), &command) != ParseError::OK);
    }
    assert(parse(request(profile, "prepurge_ms", ",\"prepurge_ms\":18446744073709551616"), &command) !=
           ParseError::OK);
    assert(parse(request(profile, "maximum_air_milli_c", ",\"maximum_air_milli_c\":2147483648"), &command) !=
           ParseError::OK);
    assert(parse(request(profile, "", ","), &command) != ParseError::OK);

    Settings settings{};
    settings.room_setpoint_milli_c = 26000;
    Controller clean({}, settings, 7, Startup::CLEAN);
    clean.tick(input(100));
    auto version = clean.snapshot(100).state_version;
    assert(clean.install_safety_envelope(profile, false, 120));
    auto s = clean.tick(input(120));
    assert(s.state == State::IDLE && s.state_version > version);
    assert(!s.outputs.fan_request && !s.outputs.glow && !s.outputs.fuel);
    assert(s.start_block == Block::MODEL_UNAVAILABLE && s.commissioning.block == Block::NONE);
    assert(clean.settings().room_setpoint_milli_c == 26000);
    assert(clean.install_learned_model(model()));
    auto changed = profile;
    changed.maximum_exhaust_milli_c += 1000;
    assert(clean.install_safety_envelope(changed, false, 140));
    s = clean.tick(input(140));
    assert(!s.learned_model.valid && s.start_block == Block::MODEL_UNAVAILABLE);
    assert(!s.commissioning.active && s.commissioning.phase == CommissioningPhase::IDLE);
    assert(std::strcmp(clean.events()[clean.event_count() - 1].kind, "PROFILE_SAVED") == 0);
    Controller reboot(changed, settings, 8, Startup::CLEAN);
    assert(!reboot.install_learned_model(model())); // An old model cannot match new limits on reboot.

    Controller interrupted({}, settings, 8, Startup::RECOVER);
    assert(interrupted.install_safety_envelope(profile, true, 100));
    s = interrupted.tick(input(100));
    assert(s.state == State::RECOVERY_PURGE && s.outputs.fan_request);
    off(s);
    assert(!interrupted.install_safety_envelope(changed, true, 120));
    auto burning = ignition();
    assert(!burning.install_safety_envelope(changed, false, kIgnitionAt));
    auto learning = ignition(true);
    assert(!learning.install_safety_envelope(changed, false, kIgnitionAt));
    auto invalid = profile;
    invalid.preheat_ms = kPreheatMinimumMs - 1;
    assert(!clean.install_safety_envelope(invalid, false, 160));
    invalid = profile;
    invalid.exploration_floor_code = 0;
    assert(!clean.install_safety_envelope(invalid, false, 160));
    invalid = profile;
    invalid.maximum_air_milli_c = invalid.maximum_purge_exit_air_milli_c;
    assert(!clean.install_safety_envelope(invalid, false, 160));

    // A fault racing a durable save is preserved when the stored profile is installed.
    auto fault = input(160);
    fault.external_faults = FAULT_CONTROL_WATCHDOG;
    clean.tick(fault);
    assert(clean.install_safety_envelope(profile, false, 160));
    s = clean.snapshot(160);
    assert(s.state == State::FAULT && s.faults == FAULT_CONTROL_WATCHDOG);
    for (bool recover : {false, true}) {
        Controller first({}, settings, 9, Startup::CLEAN);
        first.tick(fault);
        assert(first.install_safety_envelope(profile, recover, 160));
        s = first.snapshot(160);
        assert(s.faults == FAULT_CONTROL_WATCHDOG);
        assert(s.state == (recover ? State::FAULT_PURGE : State::FAULT));
        off(s);
    }

    RuntimeView view{};
    view.startup_ready = true;
    for (State state : {State::UNCONFIGURED, State::IDLE}) {
        view.control.state = state;
        assert(command_policy::block(view, Action::PROFILE) == Block::NONE);
    }
    for (State state :
         {State::RECOVERY_PURGE, State::PREPURGE, State::PREHEAT, State::IGNITION, State::VERIFY, State::RUN,
          State::STOP_PURGE, State::FAULT_PURGE, State::FAULT, State::MAINTENANCE}) {
        view.control.state = state;
        assert(command_policy::block(view, Action::PROFILE) != Block::NONE);
    }
    view.control.state = State::IDLE;
    for (bool *flag :
         {&view.storage_busy, &view.control.commissioning.active, &view.control.outputs.fan_request,
          &view.control.outputs.fuel, &view.control.outputs.glow}) {
        *flag = true;
        assert(command_policy::block(view, Action::PROFILE) == Block::BUSY);
        *flag = false;
    }
    view.storage_ok = false;
    assert(command_policy::block(view, Action::PROFILE) == Block::STORAGE_UNAVAILABLE);
    view.storage_ok = true;
    view.control.faults = FAULT_STORAGE;
    assert(command_policy::block(view, Action::PROFILE) == Block::FAULT_LATCHED);
    view.control.faults = 0;
    view.local_controls_initialized = true; // Driver init does not establish presence.
    char output[kStateJsonCapacity]{};
    JsonBuilder json(output, sizeof(output));
    assert(build_state_json(view, {}, &json));
    assert(std::strstr(output, "\"local_controls_initialized\":true"));
    assert(!std::strstr(output, "local_controls_ready"));
    assert(std::strstr(output, "\"preheat_ms\":30000"));
    assert(std::strstr(output, "\"postpurge_ms\":150000"));
    assert(std::strstr(output, "\"maximum_exhaust_milli_c\":0"));
    assert(std::strstr(output, "\"preheat_min_ms\":30000"));
    assert(std::strstr(output, "\"PROFILE\":{\"allowed\":true"));
    std::cout << "PASS profile: strict payload, grants, save admission, model invalidation, restart, fault "
                 "race, initial defaults\n";
}
