#include "control_fixture.hpp"
#include <cassert>
#include <iostream>
#include <limits>
using namespace sirokko;
using namespace control_fixture;
int main() {
    Controller disconnected({}, Settings{}, 7, Startup::RECOVER);
    auto detached = disconnected.snapshot(0);
    assert(detached.state == State::UNCONFIGURED && !detached.purge_active && !detached.outputs.fan_request);
    auto request = input(100);
    request.start = request.start_commissioning = request.set_shutdown_timer = true;
    request.shutdown_after_ms = 10000;
    detached = disconnected.tick(request);
    assert(detached.state == State::UNCONFIGURED && !detached.faults && !detached.duration_active);
    assert(!detached.outputs.fuel && !detached.outputs.glow && !detached.commissioning.active);
    Settings preview{};
    preview.room_setpoint_milli_c = 23000;
    assert(disconnected.update_settings(preview));
    request = {};
    request.now_ms = 200;
    request.enter_maintenance = true;
    assert(disconnected.tick(request).state == State::MAINTENANCE);
    request.stop = true;
    assert(disconnected.tick(request).state == State::UNCONFIGURED);

    // Preparing a revision must not alter the active model before verified storage.
    Controller revised(envelope(), Settings{}, 1, Startup::RECOVER);
    assert(revised.install_learned_model(model()));
    assert(revised.tick(input(100)).state == State::IDLE);
    const auto before_revision = revised.snapshot(100);
    LearnedModelRevision revision{1, 1300, 1900, .004, .004};
    LearnedOperatingModel candidate{};
    assert(revised.prepare_model_revision(revision, &candidate));
    assert(candidate.minimum_speed_code == 1300 && candidate.maximum_speed_code == 1900);
    assert(candidate.provenance.revision == 2 &&
           candidate.fingerprint == learned_model_fingerprint(candidate));
    assert(revised.learned_model().fingerprint == model().fingerprint);
    assert(revised.snapshot(100).state_version == before_revision.state_version);
    revision.minimum_speed_code = 1100;
    assert(!revised.prepare_model_revision(revision, &candidate));
    revision.minimum_speed_code = 1300;
    revision.expected_revision = 0;
    assert(!revised.prepare_model_revision(revision, &candidate));
    auto revision_running = ignition();
    revision.expected_revision = 1;
    assert(!revision_running.prepare_model_revision(revision, &candidate));

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
    limits.ignition_ms = 45000;
    limits.verify_ms = 45000;
    assert(valid_safety_envelope(limits));
    ++limits.verify_ms;
    assert(!valid_safety_envelope(limits));
    Controller empty({}, Settings{}, 1, Startup::RECOVER);
    auto s = empty.tick(input(100));
    off(s);
    assert(s.start_block == Block::ENVELOPE_MISSING && !s.outputs.fan_request && !s.purge_active);
    Controller missing(envelope(), Settings{}, 1, Startup::RECOVER);
    s = missing.tick(input(100));
    assert(s.start_block == Block::MODEL_UNAVAILABLE && s.commissioning.block == Block::NONE);
    auto i = input(120);
    i.start = true;
    s = missing.tick(i);
    off(s);
    assert(s.state == State::IDLE && !s.commissioning.active);
    Controller cold(envelope(), Settings{}, 1, Startup::RECOVER);
    assert(cold.install_learned_model(model()));
    s = cold.tick(input(100, -20000, -20000));
    assert(s.state == State::IDLE && s.start_block == Block::NONE);
    i = input(120, -20000, -20000);
    i.start = true;
    s = cold.tick(i);
    assert(s.state == State::PREPURGE);
    i = input(140, kThermocoupleMinimumMilliC - 1, 20000);
    s = cold.tick(i);
    off(s);
    assert(s.faults & FAULT_EXHAUST);
    Controller idle(envelope(), Settings{}, 1, Startup::RECOVER);
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
    auto bad = model();
    bad.provenance.safety_envelope_fingerprint++;
    assert(!missing.install_learned_model(bad));
    auto calibration = ignition(true);
    i = input(kIgnitionAt + 20);
    i.stop = true;
    s = calibration.tick(i);
    off(s);
    assert(!s.commissioning.active);
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
            i.sensors.air.milli_c = 150000;
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
    auto failed = c;
    s = failed.tick(input(kIgnitionAt + 100));
    off(s);
    assert(s.faults & FAULT_IGNITION_TIMEOUT);
    // The same guard drives fault-reset admission and the transition.
    s = failed.tick(input(kIgnitionAt + 100 + kPostpurgeMinimumMs));
    assert(s.state == State::FAULT && s.reset_fault_block == Block::NONE);
    auto reset = input(kIgnitionAt + 120 + kPostpurgeMinimumMs);
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
    assert(running.tick(input(kIgnitionAt + 100, 50000, 30000)).state == State::VERIFY);
    s = running.tick(input(kIgnitionAt + 200, 50000, 30000));
    assert(s.state == State::RUN && s.outputs.fuel && !s.outputs.glow);
    auto flame = running;
    s = flame.tick(input(kIgnitionAt + 220, 30000, 30000));
    off(s);
    assert(s.faults & FAULT_FLAME_LOST);
    i = input(kIgnitionAt + 220, 50000, 30000);
    i.set_shutdown_timer = true;
    i.shutdown_after_ms = 100;
    running.tick(i);
    i = input(kIgnitionAt + 320, 50000, 30000);
    i.set_shutdown_timer = true;
    i.shutdown_after_ms = 1000;
    s = running.tick(i);
    off(s);
    assert(s.state == State::STOP_PURGE); // Expiry wins over extension.
    i = input(kIgnitionAt + 380, 60000, 30000);
    s = running.tick(i);
    assert(s.state == State::STOP_PURGE);
    s = running.tick(input(kIgnitionAt + 320 + kPostpurgeMinimumMs));
    assert(s.state == State::IDLE);
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
    std::cout << "PASS control: start/calibration, STOP, sensors, faults, purge, timer, grants\n";
}
